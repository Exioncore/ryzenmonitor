#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

// Get Module Version from Makefile if available
#ifndef RYZENMONITOR_VERSION
#define RYZENMONITOR_VERSION_STR "0.0.1"
#else
#define STRINGIFY(str) #str
#define RYZENMONITOR_VERSION_STR STRINGIFY(RYZENMONITOR_VERSION)
#endif

// Kernel Module Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Exioncore");
MODULE_DESCRIPTION("Ryzen CPU monitor");
MODULE_VERSION(RYZENMONITOR_VERSION_STR);

/**
 * @brief Helper function to request data from the SMU
 * 
 * @param pci_dev[in]   The CPU pci device
 * @param addr[in]      The address of the value we want to read
 * @param value[out]    The resulting value
 * @return int          0 if successful    
 */
static int read_from_smu(const struct pci_dev *pci_dev, u32 addr, u32* value);

/**
 * @brief Get the current temperature of the CPU die
 * 
 * @return u32 The temperature in degrees Celsius
 */
static u32 die_temperature(void);

/**
 * @brief Get the current temperature of the CCD
 * 
 * @param ccd   The CCD we want to get the temperature of
 * @return u32  The temperature in degrees Celsius
 */
static u32 ccd_temperature(u8 ccd);

/**
 * @brief Get the frequency of the CPU core
 * 
 * @param core The core we want to get the frequency of
 * @return u32 The frequency in MHz
 */
static u32 core_frequency(u16 core);

/**
 * @brief Get the number of CCDs in the CPU
 * 
 * @return u8 The number of CCDs
 */
static u8 number_of_ccds(void);

/**
 * @brief Get the number of threads per core
 * 
 * @return u8 The number of threads per core
 */
static u8 num_threads_per_core(void);

/**
 * @brief Get the energy unit of the CPU
 * 
 * @return u32 The energy unit
 */
static u32 energy_unit(void);

/**
 * @brief The accumulated energy consumed by the CPU
 * 
 * @return u32 The energy consumed in Joules
 */
static u32 energy(void);

/**
 * @brief Update a temperature attribute
 * 
 * @param dev 
 * @param attr 
 * @param buf 
 * @return ssize_t 
 */
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf);

/**
 * @brief Update a frequency attribute
 * 
 * @param dev 
 * @param attr 
 * @param buf 
 * @return ssize_t 
 */
static ssize_t frequency_show(struct device *dev, struct device_attribute *attr, char *buf);

/**
 * @brief Update a power attribute
 * 
 * @param dev 
 * @param attr 
 * @param buf 
 * @return ssize_t 
 */
static ssize_t power_show(struct device *dev, struct device_attribute *attr, char *buf);

/**
 * @brief Create the sysfs directory /sys/kernel/ryzenmonitor along with the subdirectories and attributes
 * 
 * @return int 0 If the directory was created successfully
 */
static int create_sysfs_dir(void);

/**
 * @brief Destroy the sysfs directory /sys/kernel/ryzenmonitor
 * 
 */
static void destroy_sysfs_dir(void);

/**
 * @brief Initializes the PCI driver
 * 
 * @param pdev The device for whom the device driver is being loaded
 * @param id The PCI device ID
 * @return int 0 If the driver was loaded successfully
 */
static int ryzenmonitor_probe(struct pci_dev *pdev, const struct pci_device_id *id);

/**
 * @brief Unloads the PCI driver
 * 
 * @param pdev The device for whom the device driver is being unloaded
 */
static void ryzenmonitor_remove(struct pci_dev *pdev);