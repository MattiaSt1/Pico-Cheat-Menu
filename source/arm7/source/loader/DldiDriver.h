#pragma once
#include "dldiHeader.h"

/// @brief Class representing a DLDI driver.
class DldiDriver
{
    dldi_header_t* _driver;
public:
    explicit DldiDriver(dldi_header_t* driver)
        : _driver(driver) { }

    /// @brief Returns whether the driver has been relocated to its current ram address.
    /// @return True if the driver is relocated to its current ram address, or false otherwise.
    bool IsRelocated() const
    {
        return _driver->driverStartAddress == (u32)_driver;
    }

    /// @brief Relocates the driver to its current ram address.
    void Relocate()
    {
        Relocate((u32)_driver);
    }

    /// @brief Relocates the driver to the given targetAddress.
    /// @param targetAddress The address to relocate the driver to.
    void Relocate(u32 targetAddress);

    /// @brief Clears the bss area of the driver if needed, making it ready for use.
    ///        This method should only be used after placing the driver in a memory region
    ///        with enough space and relocating it.
    void PrepareForUse();

    /// @brief Patches this dldi driver into the given stub.
    /// @param stub The stub to patch the dldi driver into.
    /// @return \c true if patching was successful, or \c false otherwise.
    bool PatchTo(dldi_header_t* stub);

    /// @brief Calls the startup function of the DLDI driver and returns the result.
    /// @return \c true if startup was successful, or \c false otherwise.
    bool Startup()
    {
        return ((dldi_startup_func_t)_driver->startupFuncAddress)();
    }

    /// @brief Calls the is inserted function of the DLDI driver and returns the result.
    /// @return \c true if the storage medium is inserted, or \c false otherwise.
    bool IsInserted()
    {
        return ((dldi_inserted_func_t)_driver->isInsertedFuncAddress)();
    }

    /// @brief Reads sectors using this DLDI driver.
    /// @param sector The sector to start reading at.
    /// @param count The number of sectors to read.
    /// @param dst The destination buffer.
    /// @return \c true if reading was successful, or \c false otherwise.
    bool ReadSectors(u32 sector, u32 count, void* dst)
    {
        return ((dldi_read_func_t)_driver->readSectorsFuncAddress)(sector, count, dst);
    }

    /// @brief Writes sectors using this DLDI driver.
    /// @param sector The sector to start writing at.
    /// @param count The number of sectors to write.
    /// @param src The source buffer.
    /// @return \c true if writing was successful, or \c false otherwise.
    bool WriteSectors(u32 sector, u32 count, const void* src)
    {
        return ((dldi_write_func_t)_driver->writeSectorsFuncAddress)(sector, count, src);
    }

    /// @brief Calls the clear status function of the DLDI driver and returns the result.
    /// @return \c true if clear status was successful, or \c false otherwise.
    bool ClearStatus()
    {
        return ((dldi_clearstatus_func_t)_driver->clearStatusFuncAddress)();
    }

    /// @brief Calls the shutdown function of the DLDI driver and returns the result.
    /// @return \c true if shutdown was successful, or \c false otherwise.
    bool Shutdown()
    {
        return ((dldi_shutdown_func_t)_driver->shutdownFuncAddress)();
    }
};
