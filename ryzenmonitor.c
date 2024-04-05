#include "ryzenmonitor.h"
// System Includes
#include <asm/amd_nb.h>
#include <linux/math.h>

/////////////////////
#define CCD_NAME_LEN        6
#define CORE_FREQ_NAME_LEN  12

///////////////////
// CPU Registers //
///////////////////
#define CPUID_NUM_THREADS_PER_CORE      0x8000001E
#define MSR_CORE_FREQ_ADDR              0xC0010293
#define MSR_ENERGY_UNIT_ADDR            0xC0010299
#define MSR_POWER_ADDR                  0xC001029B
#define SMU_TDIE_ADDR                   0x00059800
#define SMU_TCCD_ADDR(x)               (0x00059b08 + ((x) * 4))

/////////////////////
// Data Structures //
/////////////////////
struct temperature_attribute_t {
    struct device_attribute dev_attr;
    int8_t ccd;                                 // Context data for the temperature attribute
};

struct frequency_attribute_t {
    struct device_attribute dev_attr;
    char name[CORE_FREQ_NAME_LEN];              // Name of the attribute
    u16 core_id;                                // Context data for the frequency attribute
};

struct power_attribute_t {
    struct device_attribute dev_attr;
    ktime_t last_update;                        // Context data for the power attribute (last update time)
    u32 last_energy;                            // Context data for the power attribute (last energy value)
};

struct ccd_data_t {
    char name[CCD_NAME_LEN];                    // Name of the directory
    struct kobject *directory_kobj;             // CCD Root Directory (/sys/kernel/ryzenmonitor/ccd*)
    struct frequency_attribute_t *frequency;    // Each CCD Core frequency (/sys/kernel/ryzenmonitor/ccd*/cpu*_freq)
    struct temperature_attribute_t temperature; // CCD temperature (/sys/kernel/ryzenmonitor/ccd*/temperature)
};

struct cpu_data_t {
    struct pci_dev *pci_dev;                    // The CPU pci device
    u8 n_ccds;                                  // Number of CCDs
    u8 n_cores_per_ccd;                         // Number of cores per CCD
    u32 energy_unit;                            // Energy unit in Joules

    struct kobject* directory_kobj;             // Root directory (/sys/kernel/ryzenmonitor)
    struct temperature_attribute_t temperature; // CPU Temperature attribute (/sys/kernel/ryzenmonitor/temperature)
    struct power_attribute_t power;             // CPU Power attribute (/sys/kernel/ryzenmonitor/power)
    struct ccd_data_t* ccds;                    // Each CCD directory structure (/sys/kernel/ryzenmonitor/ccd*)
};

///////////////
// Variables //
///////////////
static DEFINE_MUTEX(smu_mtx);   // Mutex to guard against simultaneous SMU access
struct cpu_data_t cpu_data;     // This is the SysFS structure of the kernel module (/sys/kernel/ryzenmonitor)

////////////////////
// Helper Methods //
////////////////////
static int read_from_smu(const struct pci_dev *pci_dev, u32 addr, u32* value) {
    mutex_lock(&smu_mtx);
    if (pci_bus_write_config_dword(pci_dev->bus, pci_dev->devfn, 0x60, addr)) {
        mutex_unlock(&smu_mtx);
        pr_err("Ryzen Monitor: Failed to write %x to PCI config space %x\n", addr, 0x60);
        return 1;
    }
    if (pci_bus_read_config_dword(pci_dev->bus, pci_dev->devfn, 0x64, value)) {
        mutex_unlock(&smu_mtx);
        pr_err("Ryzen Monitor: Failed to read PCI config space %x\n", 0x64);
        return 1;
    }
    mutex_unlock(&smu_mtx);
    return 0;
}

/////////////////////////////
// CPU Information getters //
/////////////////////////////
static u32 die_temperature(void) {
    u32 value;
    if (!read_from_smu(cpu_data.pci_dev, SMU_TDIE_ADDR, &value)) {
        return (((value >> 21) & 0x7FF) / 8) - 49;
    }
    return 0;
}

static u32 ccd_temperature(u8 ccd) {
    u32 value;
    if (!read_from_smu(cpu_data.pci_dev, SMU_TCCD_ADDR(ccd), &value)) {
        return ((value & 0xFFF) * 125 - 305000) / 1000;
    }
    return U32_MAX;
}

static u32 core_frequency(u16 core) {
    u32 eax, edx;
    if (!rdmsr_on_cpu(core, MSR_CORE_FREQ_ADDR, &eax, &edx)) {
        return (((eax & 0xFF) / ((eax >> 8) & 0x3F)) * 200);
    }
    return 0;
}

static u8 number_of_ccds(void) {
    u8 n_ccds = 0;
    for (; n_ccds < 8; n_ccds++) {
        // Check if temperature value is within AMD specifications (0C < T < 225C)
        if (ccd_temperature(n_ccds) > 225) {
            break;   
        }
    }
    return n_ccds;
}

static u8 num_threads_per_core(void) {
    u32 ebx = cpuid_ebx(CPUID_NUM_THREADS_PER_CORE);
    return (((ebx >> 8) & 0xFF) + 1);
}

static u32 energy_unit(void) {
    u32 eax, edx;
    if (!rdmsr_on_cpu(0, MSR_ENERGY_UNIT_ADDR, &eax, &edx)) {
        eax = ((eax >> 8) & 0x1F);
        eax = int_pow(2, eax);
        return eax;
    } else {
        return U32_MAX;
    }
}

static u32 energy(void) {
    u32 eax, edx;
    if (!rdmsr_on_cpu(0, MSR_POWER_ADDR, &eax, &edx)) {
        return eax;
    } else {
        return U32_MAX;
    }
}

////////////////////////////////////
// Attributes Show Implementation //
////////////////////////////////////
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf) {
    // Retrieve CCD number associated with this attribute
    int8_t ccd = container_of(attr, struct temperature_attribute_t, dev_attr)->ccd;
    // Get temperature
    u32 temp = ccd == -1 ? die_temperature() : ccd_temperature(ccd);
    return sprintf(buf, "%d\n", temp);
}

static ssize_t frequency_show(struct device *dev, struct device_attribute *attr, char *buf) {
    // Retrieve Core number associated with this attribute
    u16 core = container_of(attr, struct frequency_attribute_t, dev_attr)->core_id;
    // Get frequency
    u32 freq = core_frequency(core);
    return sprintf(buf, "%d\n", freq);
}

static ssize_t power_show(struct device *dev, struct device_attribute *attr, char *buf) {
    // Retrieve context data
    struct power_attribute_t *power = container_of(attr, struct power_attribute_t, dev_attr);
    // Get current time and energy
    ktime_t now = ktime_get();
    u32 energy_now = energy();
    // Calculate time delta and energy delta
    ktime_t time_delta = ktime_sub(now, power->last_update);  
    u32 energy_delta = energy_now - power->last_energy;
    // Calculate power in Watts
    u64 power_now = (energy_delta / cpu_data.energy_unit);
    power_now = power_now * 1000000000 / ktime_to_ns(time_delta);
    // Update power attribute
    power->last_update = now;
    power->last_energy = energy_now;
    return sprintf(buf, "%lld\n", power_now);
}

/////////////////////
// Sysfs Directory //
/////////////////////
static int create_sysfs_dir(void) {
    u16 curr_core = 0;
    cpu_data.directory_kobj = kobject_create_and_add("ryzenmonitor", kernel_kobj);
    if (cpu_data.directory_kobj != NULL) {
        // Temperature attribute
        cpu_data.temperature = (struct temperature_attribute_t) {
            .dev_attr = (struct device_attribute) {
                .attr = { .name = "temperature", .mode = 0444 },
                .show = temperature_show,
            },
            .ccd = -1
        };
        if (sysfs_create_file(cpu_data.directory_kobj, &cpu_data.temperature.dev_attr.attr)) {
            pr_err("Ryzen Monitor: Failed to create sysfs temperature file\n");
            return 1;
        }
        // Power attribute
        cpu_data.power = (struct power_attribute_t) {
            .dev_attr = (struct device_attribute) {
                .attr = { .name = "power", .mode = 0444 },
                .show = power_show,
            },
            .last_energy = energy(),
            .last_update = ktime_get()
        };
        if (sysfs_create_file(cpu_data.directory_kobj, &cpu_data.power.dev_attr.attr)) {
            pr_err("Ryzen Monitor: Failed to create sysfs power file\n");
            return 1;
        }
        // CCDs
        cpu_data.ccds = kmalloc_array(cpu_data.n_ccds, sizeof(struct ccd_data_t), GFP_KERNEL);
        for (uint8_t ccd_i = 0; ccd_i < cpu_data.n_ccds; ccd_i++) {
            // Get CCD and set name
            struct ccd_data_t *ccd = &cpu_data.ccds[ccd_i];
            snprintf(ccd->name, 6, "ccd%d", ccd_i);
            // Create CCD directory (/sys/kernel/ryzenmonitor/ccd*)
            ccd->directory_kobj = kobject_create_and_add(ccd->name, cpu_data.directory_kobj);
            if (ccd->directory_kobj != NULL) {
                // Temperature attribute (/sys/kernel/ryzenmonitor/ccd*/temperature)
                ccd->temperature = (struct temperature_attribute_t) {
                    .dev_attr = (struct device_attribute) {
                        .attr = { .name = "temperature", .mode = 0444 },
                        .show = temperature_show,
                    },
                    .ccd = ccd_i
                };
                if (sysfs_create_file(ccd->directory_kobj, &ccd->temperature.dev_attr.attr)) {
                    pr_err("Ryzen Monitor: Failed to create sysfs CCD %d temperature file\n", ccd_i);
                    return 1;
                }
                // Frequency attributes (/sys/kernel/ryzenmonitor/ccd*/cpu*_freq)
                ccd->frequency = kmalloc_array(cpu_data.n_cores_per_ccd, sizeof(struct frequency_attribute_t), GFP_KERNEL);
                for (uint8_t core_i = 0; core_i < cpu_data.n_cores_per_ccd; core_i++) {
                    // Get Core Freq Attribute and set name
                    struct frequency_attribute_t *core_freq = &ccd->frequency[core_i];
                    snprintf(core_freq->name, 16, "cpu%d_freq", core_i);
                    core_freq->dev_attr = (struct device_attribute) {
                        .attr = { .name = core_freq->name, .mode = 0444 },
                        .show = frequency_show,
                    };
                    core_freq->core_id = curr_core++;
                    if (sysfs_create_file(ccd->directory_kobj, &core_freq->dev_attr.attr)) {
                        pr_err("Ryzen Monitor: Failed to create sysfs CCD %d Core %d frequency file\n", ccd_i, core_i);
                        return 1;
                    }
                }
            } else {
                pr_err("Ryzen Monitor: Failed to create sysfs ccd %d directory\n", ccd_i);
                return 1;
            }
        }

    } else {
        pr_err("Ryzen Monitor: Failed to create sysfs ryzenmonitor directory\n");
        return 1;
    }
    return 0;
}

static void destroy_sysfs_dir(void) {
     if (cpu_data.ccds != NULL) {
        for (u8 ccd_i = 0; ccd_i < cpu_data.n_ccds; ccd_i++) {
            struct ccd_data_t *ccd = &cpu_data.ccds[ccd_i];
            if (ccd->directory_kobj != NULL) {
                if (ccd->frequency != NULL) {
                    // Remove Core frequency attributes (/sys/kernel/ryzenmonitor/ccd*/cpu*_freq)
                    for (u16 core = 0; core < cpu_data.n_cores_per_ccd; core++) {
                        sysfs_remove_file(ccd->directory_kobj, &ccd->frequency[core].dev_attr.attr);
                    }
                    // Free frequency attributes array
                    kfree(ccd->frequency);
                }
                // Remove CCD temperature attribute (/sys/kernel/ryzenmonitor/ccd*/temperature)
                sysfs_remove_file(ccd->directory_kobj, &ccd->temperature.dev_attr.attr);
                // Remove CCD directory (/sys/kernel/ryzenmonitor/ccd*)
                kobject_del(ccd->directory_kobj);
            }
        }
        // Free CCDs array
        kfree(cpu_data.ccds);
    }
    if (cpu_data.directory_kobj != NULL) {
        // Remove CPU temperature and power attributes (/sys/kernel/ryzenmonitor/temperature, /sys/kernel/ryzenmonitor/power)
        sysfs_remove_file(cpu_data.directory_kobj, &cpu_data.temperature.dev_attr.attr);
        sysfs_remove_file(cpu_data.directory_kobj, &cpu_data.power.dev_attr.attr);
        // Remove CPU directory (/sys/kernel/ryzenmonitor)
        kobject_del(cpu_data.directory_kobj);
    }
}

///////////////////////
// Define PCI driver //
///////////////////////
static const struct pci_device_id ryzenmonitor_pci_tbl[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x14d8) },                           // Ryzen 7000 / Ryzen 7000X3D
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_DF_F3) },      // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M10H_DF_F3) }, // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M40H_DF_F3) }, // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M50H_DF_F3) }, // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M60H_DF_F3) }, // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M70H_DF_F3) }, // Zen 3/4 ?
    // { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M78H_DF_F3) }, // Zen 3/4 ?
    { 0 },
    // To add support for a new device you need to add a new entry here (linux/pci_ids.h contains these macros)
    // Alternatively, the entry 00:00:00 of lspci contains the device_id of the CPU in your system
    // Example: 00:00.0 Host bridge: Advanced Micro Devices, Inc. [AMD] Device 14d8
};
MODULE_DEVICE_TABLE(pci, ryzenmonitor_pci_tbl);

static int ryzenmonitor_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    pr_info("Ryzen Monitor: Loading\n");
    // Store the PCI device and determine CPU topology
    cpu_data.pci_dev = pdev;
    cpu_data.n_ccds = number_of_ccds();
    pr_info("Ryzen Monitor: Detected %d CCDs\n", cpu_data.n_ccds);
    unsigned int n_logical_cores = num_online_cpus();
    unsigned int n_physical_cores = n_logical_cores / num_threads_per_core();
    pr_info("Ryzen Monitor: Detected %d Logical Cores on %d Physical Cores\n", n_logical_cores, n_physical_cores);
    cpu_data.n_cores_per_ccd = n_physical_cores / cpu_data.n_ccds;
    cpu_data.energy_unit = energy_unit();
    // Create SysFS
    if (!create_sysfs_dir()) {
        pr_info("Ryzen Monitor: Loaded\n");
        return 0;
    } else {
        pr_err("Ryzen Monitor: Failed to load\n");
        return 1;
    }
}

static void ryzenmonitor_remove(struct pci_dev *pdev) {
    pr_info("Ryzen Monitor: Unloading\n");
    // Destroy SysFS
    destroy_sysfs_dir();
    pr_info("Ryzen Monitor: Unloaded\n");
}

static struct pci_driver ryzenmonitor_pci_driver = {
    .name = "ryzenmonitor",
    .id_table = ryzenmonitor_pci_tbl,
    .probe = ryzenmonitor_probe,
    .remove = ryzenmonitor_remove,
};

module_pci_driver(ryzenmonitor_pci_driver);