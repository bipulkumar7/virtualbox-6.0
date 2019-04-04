/* $Rev: 83248 $ */
/** @file
 * VBoxGuest - Linux specifics.
 *
 * Note. Unfortunately, the difference between this and SUPDrv-linux.c is
 *       a little bit too big to be helpful.
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * Some lines of code to disable the local APIC on x86_64 machines taken
 * from a Mandriva patch by Gwenole Beauchesne <gbeauchesne@mandriva.com>.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
#include "the-linux-kernel.h"
#include "VBoxGuestInternal.h"
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include "version-generated.h"
#include "product-generated.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/process.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <VBox/log.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The device name. */
#define DEVICE_NAME             "vboxguest"
/** The device name for the device node open to everyone.. */
#define DEVICE_NAME_USER        "vboxuser"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
# define PCI_DEV_GET(v,d,p)     pci_get_device(v,d,p)
# define PCI_DEV_PUT(x)         pci_dev_put(x)
#else
# define PCI_DEV_GET(v,d,p)     pci_find_device(v,d,p)
# define PCI_DEV_PUT(x)         do {} while(0)
#endif

/* 2.4.x compatibility macros that may or may not be defined. */
#ifndef IRQ_RETVAL
# define irqreturn_t            void
# define IRQ_RETVAL(n)
#endif


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int  vboxguestLinuxModInit(void);
static void vboxguestLinuxModExit(void);
static int  vboxguestLinuxOpen(struct inode *pInode, struct file *pFilp);
static int  vboxguestLinuxRelease(struct inode *pInode, struct file *pFilp);
#ifdef HAVE_UNLOCKED_IOCTL
static long vboxguestLinuxIOCtl(struct file *pFilp, unsigned int uCmd, unsigned long ulArg);
#else
static int  vboxguestLinuxIOCtl(struct inode *pInode, struct file *pFilp, unsigned int uCmd, unsigned long ulArg);
#endif
static int  vboxguestFAsync(int fd, struct file *pFile, int fOn);
static unsigned int vboxguestPoll(struct file *pFile, poll_table *pPt);
static ssize_t vboxguestRead(struct file *pFile, char *pbBuf, size_t cbRead, loff_t *poff);


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/**
 * Device extention & session data association structure.
 */
static VBOXGUESTDEVEXT          g_DevExt;
/** The PCI device. */
static struct pci_dev          *g_pPciDev;
/** The base of the I/O port range. */
static RTIOPORT                 g_IOPortBase;
/** The base of the MMIO range. */
static RTHCPHYS                 g_MMIOPhysAddr = NIL_RTHCPHYS;
/** The size of the MMIO range as seen by PCI. */
static uint32_t                 g_cbMMIO;
/** The pointer to the mapping of the MMIO range. */
static void                    *g_pvMMIOBase;
/** Wait queue used by polling. */
static wait_queue_head_t        g_PollEventQueue;
/** Asynchronous notification stuff.  */
static struct fasync_struct    *g_pFAsyncQueue;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
/** Whether we've create the logger or not. */
static volatile bool            g_fLoggerCreated;
/** Release logger group settings. */
static char                     g_szLogGrp[128];
/** Release logger flags settings. */
static char                     g_szLogFlags[128];
/** Release logger destination settings. */
static char                     g_szLogDst[128];
# if 0
/** Debug logger group settings. */
static char                     g_szDbgLogGrp[128];
/** Debug logger flags settings. */
static char                     g_szDbgLogFlags[128];
/** Debug logger destination settings. */
static char                     g_szDbgLogDst[128];
# endif
#endif

/** Our file node major id.
 * Either set dynamically at run time or statically at compile time. */
#ifdef CONFIG_VBOXGUEST_MAJOR
static unsigned int             g_iModuleMajor = CONFIG_VBOXGUEST_MAJOR;
#else
static unsigned int             g_iModuleMajor = 0;
#endif
#ifdef CONFIG_VBOXADD_MAJOR
# error "CONFIG_VBOXADD_MAJOR -> CONFIG_VBOXGUEST_MAJOR"
#endif

/** The file_operations structure. */
static struct file_operations   g_FileOps =
{
    owner:          THIS_MODULE,
    open:           vboxguestLinuxOpen,
    release:        vboxguestLinuxRelease,
#ifdef HAVE_UNLOCKED_IOCTL
    unlocked_ioctl: vboxguestLinuxIOCtl,
#else
    ioctl:          vboxguestLinuxIOCtl,
#endif
    fasync:         vboxguestFAsync,
    read:           vboxguestRead,
    poll:           vboxguestPoll,
    llseek:         no_llseek,
};

/** The miscdevice structure. */
static struct miscdevice        g_MiscDevice =
{
    minor:          MISC_DYNAMIC_MINOR,
    name:           DEVICE_NAME,
    fops:           &g_FileOps,
};

/** The file_operations structure for the user device.
 * @remarks For the time being we'll be using the same implementation as
 *          /dev/vboxguest here. */
static struct file_operations   g_FileOpsUser =
{
    owner:          THIS_MODULE,
    open:           vboxguestLinuxOpen,
    release:        vboxguestLinuxRelease,
#ifdef HAVE_UNLOCKED_IOCTL
    unlocked_ioctl: vboxguestLinuxIOCtl,
#else
    ioctl:          vboxguestLinuxIOCtl,
#endif
};

/** The miscdevice structure for the user device. */
static struct miscdevice        g_MiscDeviceUser =
{
    minor:          MISC_DYNAMIC_MINOR,
    name:           DEVICE_NAME_USER,
    fops:           &g_FileOpsUser,
};


/** PCI hotplug structure. */
static const struct pci_device_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
__devinitdata
#endif
g_VBoxGuestPciId[] =
{
    {
        vendor:     VMMDEV_VENDORID,
        device:     VMMDEV_DEVICEID
    },
    {
        /* empty entry */
    }
};
MODULE_DEVICE_TABLE(pci, g_VBoxGuestPciId);


/**
 * Converts a VBox status code to a linux error code.
 *
 * @returns corresponding negative linux error code.
 * @param   rc  supdrv error code (SUPDRV_ERR_* defines).
 */
static int vboxguestLinuxConvertToNegErrno(int rc)
{
    if (   rc > -1000
        && rc < 1000)
        return -RTErrConvertToErrno(rc);
    switch (rc)
    {
        case VERR_HGCM_SERVICE_NOT_FOUND:      return -ESRCH;
        case VINF_HGCM_CLIENT_REJECTED:        return 0;
        case VERR_HGCM_INVALID_CMD_ADDRESS:    return -EFAULT;
        case VINF_HGCM_ASYNC_EXECUTE:          return 0;
        case VERR_HGCM_INTERNAL:               return -EPROTO;
        case VERR_HGCM_INVALID_CLIENT_ID:      return -EINVAL;
        case VINF_HGCM_SAVE_STATE:             return 0;
        /* No reason to return this to a guest */
        // case VERR_HGCM_SERVICE_EXISTS:         return -EEXIST;
        default:
            AssertMsgFailed(("Unhandled error code %Rrc\n", rc));
            return -EPROTO;
    }
}



/**
 * Does the PCI detection and init of the device.
 *
 * @returns 0 on success, negated errno on failure.
 */
static int __init vboxguestLinuxInitPci(void)
{
    struct pci_dev *pPciDev;
    int             rc;

    pPciDev = PCI_DEV_GET(VMMDEV_VENDORID, VMMDEV_DEVICEID, NULL);
    if (pPciDev)
    {
        rc = pci_enable_device(pPciDev);
        if (rc >= 0)
        {
            /* I/O Ports are mandatory, the MMIO bit is not. */
            g_IOPortBase = pci_resource_start(pPciDev, 0);
            if (g_IOPortBase != 0)
            {
                /*
                 * Map the register address space.
                 */
                g_MMIOPhysAddr = pci_resource_start(pPciDev, 1);
                g_cbMMIO       = pci_resource_len(pPciDev, 1);
                if (request_mem_region(g_MMIOPhysAddr, g_cbMMIO, DEVICE_NAME) != NULL)
                {
                    g_pvMMIOBase = ioremap(g_MMIOPhysAddr, g_cbMMIO);
                    if (g_pvMMIOBase)
                    {
                        /** @todo why aren't we requesting ownership of the I/O ports as well? */
                        g_pPciDev = pPciDev;
                        return 0;
                    }

                    /* failure cleanup path */
                    LogRel((DEVICE_NAME ": ioremap failed; MMIO Addr=%RHp cb=%#x\n", g_MMIOPhysAddr, g_cbMMIO));
                    rc = -ENOMEM;
                    release_mem_region(g_MMIOPhysAddr, g_cbMMIO);
                }
                else
                {
                    LogRel((DEVICE_NAME ": failed to obtain adapter memory\n"));
                    rc = -EBUSY;
                }
                g_MMIOPhysAddr = NIL_RTHCPHYS;
                g_cbMMIO       = 0;
                g_IOPortBase   = 0;
            }
            else
            {
                LogRel((DEVICE_NAME ": did not find expected hardware resources\n"));
                rc = -ENXIO;
            }
            pci_disable_device(pPciDev);
        }
        else
            LogRel((DEVICE_NAME ": could not enable device: %d\n", rc));
        PCI_DEV_PUT(pPciDev);
    }
    else
    {
        printk(KERN_ERR DEVICE_NAME ": VirtualBox Guest PCI device not found.\n");
        rc = -ENODEV;
    }
    return rc;
}


/**
 * Clean up the usage of the PCI device.
 */
static void vboxguestLinuxTermPci(void)
{
    struct pci_dev *pPciDev = g_pPciDev;
    g_pPciDev = NULL;
    if (pPciDev)
    {
        iounmap(g_pvMMIOBase);
        g_pvMMIOBase = NULL;

        release_mem_region(g_MMIOPhysAddr, g_cbMMIO);
        g_MMIOPhysAddr = NIL_RTHCPHYS;
        g_cbMMIO = 0;

        pci_disable_device(pPciDev);
    }
}


/**
 * Interrupt service routine.
 *
 * @returns In 2.4 it returns void.
 *          In 2.6 we indicate whether we've handled the IRQ or not.
 *
 * @param   iIrq            The IRQ number.
 * @param   pvDevId         The device ID, a pointer to g_DevExt.
 * @param   pvRegs          Register set. Removed in 2.6.19.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static irqreturn_t vboxguestLinuxISR(int iIrrq, void *pvDevId)
#else
static irqreturn_t vboxguestLinuxISR(int iIrrq, void *pvDevId, struct pt_regs *pRegs)
#endif
{
    bool fTaken = VBoxGuestCommonISR(&g_DevExt);
    return IRQ_RETVAL(fTaken);
}


/**
 * Registers the ISR and initializes the poll wait queue.
 */
static int __init vboxguestLinuxInitISR(void)
{
    int rc;

    init_waitqueue_head(&g_PollEventQueue);
    rc = request_irq(g_pPciDev->irq,
                     vboxguestLinuxISR,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
                     IRQF_SHARED,
#else
                     SA_SHIRQ,
#endif
                     DEVICE_NAME,
                     &g_DevExt);
    if (rc)
    {
        LogRel((DEVICE_NAME ": could not request IRQ %d: err=%d\n", g_pPciDev->irq, rc));
        return rc;
    }
    return 0;
}


/**
 * Deregisters the ISR.
 */
static void vboxguestLinuxTermISR(void)
{
    free_irq(g_pPciDev->irq, &g_DevExt);
}


/**
 * Creates the device nodes.
 *
 * @returns 0 on success, negated errno on failure.
 */
static int __init vboxguestLinuxInitDeviceNodes(void)
{
    int rc;

    /*
     * The full feature device node.
     */
    if (g_iModuleMajor > 0)
    {
        rc = register_chrdev(g_iModuleMajor, DEVICE_NAME, &g_FileOps);
        if (rc < 0)
        {
            LogRel((DEVICE_NAME ": register_chrdev failed: g_iModuleMajor: %d, rc: %d\n", g_iModuleMajor, rc));
            return rc;
        }
    }
    else
    {
        rc = misc_register(&g_MiscDevice);
        if (rc)
        {
            LogRel((DEVICE_NAME ": misc_register failed for %s (rc=%d)\n", DEVICE_NAME, rc));
            return rc;
        }
    }

    /*
     * The device node intended to be accessible by all users.
     */
    rc = misc_register(&g_MiscDeviceUser);
    if (rc)
    {
        LogRel((DEVICE_NAME ": misc_register failed for %s (rc=%d)\n", DEVICE_NAME_USER, rc));
        if (g_iModuleMajor > 0)
            unregister_chrdev(g_iModuleMajor, DEVICE_NAME);
        else
            misc_deregister(&g_MiscDevice);
        return rc;
    }

    return 0;
}


/**
 * Deregisters the device nodes.
 */
static void vboxguestLinuxTermDeviceNodes(void)
{
    if (g_iModuleMajor > 0)
        unregister_chrdev(g_iModuleMajor, DEVICE_NAME);
    else
        misc_deregister(&g_MiscDevice);
    misc_deregister(&g_MiscDeviceUser);
}



/**
 * Initialize module.
 *
 * @returns appropriate status code.
 */
static int __init vboxguestLinuxModInit(void)
{
    static const char * const   s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    PRTLOGGER                   pRelLogger;
    int                         rc;

    /*
     * Initialize IPRT first.
     */
    rc = RTR0Init(0);
    if (RT_FAILURE(rc))
    {
        printk(KERN_ERR DEVICE_NAME ": RTR0Init failed, rc=%d.\n", rc);
        return -EINVAL;
    }

    /*
     * Create the release log.
     * (We do that here instead of common code because we want to log
     * early failures using the LogRel macro.)
     */
    rc = RTLogCreate(&pRelLogger, 0 /* fFlags */, "all",
                     "VBOX_RELEASE_LOG", RT_ELEMENTS(s_apszGroups), s_apszGroups,
                     RTLOGDEST_STDOUT | RTLOGDEST_DEBUGGER | RTLOGDEST_USER, NULL);
    if (RT_SUCCESS(rc))
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
        RTLogGroupSettings(pRelLogger, g_szLogGrp);
        RTLogFlags(pRelLogger, g_szLogFlags);
        RTLogDestinations(pRelLogger, g_szLogDst);
#endif
        RTLogRelSetDefaultInstance(pRelLogger);
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
    g_fLoggerCreated = true;
#endif

    /*
     * Locate and initialize the PCI device.
     */
    rc = vboxguestLinuxInitPci();
    if (rc >= 0)
    {
        /*
         * Register the interrupt service routine for it.
         */
        rc = vboxguestLinuxInitISR();
        if (rc >= 0)
        {
            /*
             * Call the common device extension initializer.
             */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) && defined(RT_ARCH_X86)
            VBOXOSTYPE enmOSType = VBOXOSTYPE_Linux26;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) && defined(RT_ARCH_AMD64)
            VBOXOSTYPE enmOSType = VBOXOSTYPE_Linux26_x64;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0) && defined(RT_ARCH_X86)
            VBOXOSTYPE enmOSType = VBOXOSTYPE_Linux24;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0) && defined(RT_ARCH_AMD64)
            VBOXOSTYPE enmOSType = VBOXOSTYPE_Linux24_x64;
#else
# warning "huh? which arch + version is this?"
            VBOXOSTYPE enmOsType = VBOXOSTYPE_Linux;
#endif
            rc = VBoxGuestInitDevExt(&g_DevExt,
                                     g_IOPortBase,
                                     g_pvMMIOBase,
                                     g_cbMMIO,
                                     enmOSType,
                                     VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Finally, create the device nodes.
                 */
                rc = vboxguestLinuxInitDeviceNodes();
                if (rc >= 0)
                {
                    /* some useful information for the user but don't show this on the console */
                    LogRel((DEVICE_NAME ": major %d, IRQ %d, I/O port %RTiop, MMIO at %RHp (size 0x%x)\n",
                            g_iModuleMajor, g_pPciDev->irq, g_IOPortBase, g_MMIOPhysAddr, g_cbMMIO));
                    printk(KERN_DEBUG DEVICE_NAME ": Successfully loaded version "
                           VBOX_VERSION_STRING " (interface " RT_XSTR(VMMDEV_VERSION) ")\n");
                    return rc;
                }

                /* bail out */
                VBoxGuestDeleteDevExt(&g_DevExt);
            }
            else
            {
                LogRel((DEVICE_NAME ": VBoxGuestInitDevExt failed with rc=%Rrc\n", rc));
                rc = RTErrConvertFromErrno(rc);
            }
            vboxguestLinuxTermISR();
        }
        vboxguestLinuxTermPci();
    }
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));
    RTR0Term();
    return rc;
}


/**
 * Unload the module.
 */
static void __exit vboxguestLinuxModExit(void)
{
    /*
     * Inverse order of init.
     */
    vboxguestLinuxTermDeviceNodes();
    VBoxGuestDeleteDevExt(&g_DevExt);
    vboxguestLinuxTermISR();
    vboxguestLinuxTermPci();
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));
    RTR0Term();
}


/**
 * Device open. Called on open /dev/vboxdrv
 *
 * @param   pInode      Pointer to inode info structure.
 * @param   pFilp       Associated file pointer.
 */
static int vboxguestLinuxOpen(struct inode *pInode, struct file *pFilp)
{
    int                 rc;
    PVBOXGUESTSESSION   pSession;
    Log((DEVICE_NAME ": pFilp=%p pid=%d/%d %s\n", pFilp, RTProcSelf(), current->pid, current->comm));

    /*
     * Call common code to create the user session. Associate it with
     * the file so we can access it in the other methods.
     */
    rc = VBoxGuestCreateUserSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
        pFilp->private_data = pSession;

    Log(("vboxguestLinuxOpen: g_DevExt=%p pSession=%p rc=%d/%d (pid=%d/%d %s)\n",
         &g_DevExt, pSession, rc, vboxguestLinuxConvertToNegErrno(rc),
         RTProcSelf(), current->pid, current->comm));
    return vboxguestLinuxConvertToNegErrno(rc);
}


/**
 * Close device.
 *
 * @param   pInode      Pointer to inode info structure.
 * @param   pFilp       Associated file pointer.
 */
static int vboxguestLinuxRelease(struct inode *pInode, struct file *pFilp)
{
    Log(("vboxguestLinuxRelease: pFilp=%p pSession=%p pid=%d/%d %s\n",
         pFilp, pFilp->private_data, RTProcSelf(), current->pid, current->comm));

    VBoxGuestCloseSession(&g_DevExt, (PVBOXGUESTSESSION)pFilp->private_data);
    pFilp->private_data = NULL;
    return 0;
}


/**
 * Device I/O Control entry point.
 *
 * @param   pFilp       Associated file pointer.
 * @param   uCmd        The function specified to ioctl().
 * @param   ulArg       The argument specified to ioctl().
 */
#ifdef HAVE_UNLOCKED_IOCTL
static long vboxguestLinuxIOCtl(struct file *pFilp, unsigned int uCmd, unsigned long ulArg)
#else
static int vboxguestLinuxIOCtl(struct inode *pInode, struct file *pFilp, unsigned int uCmd, unsigned long ulArg)
#endif
{
    PVBOXGUESTSESSION   pSession = (PVBOXGUESTSESSION)pFilp->private_data;
    uint32_t            cbData   = _IOC_SIZE(uCmd);
    void               *pvBufFree;
    void               *pvBuf;
    int                 rc;
    uint64_t            au64Buf[32/sizeof(uint64_t)];

    Log6(("vboxguestLinuxIOCtl: pFilp=%p uCmd=%#x ulArg=%p pid=%d/%d\n", pFilp, uCmd, (void *)ulArg, RTProcSelf(), current->pid));

    /*
     * Buffer the request.
     */
    if (cbData <= sizeof(au64Buf))
    {
        pvBufFree = NULL;
        pvBuf = &au64Buf[0];
    }
    else
    {
        pvBufFree = pvBuf = RTMemTmpAlloc(cbData);
        if (RT_UNLIKELY(!pvBuf))
        {
            LogRel((DEVICE_NAME "::IOCtl: RTMemTmpAlloc failed to alloc %u bytes.\n", cbData));
            return -ENOMEM;
        }
    }
    if (RT_LIKELY(copy_from_user(pvBuf, (void *)ulArg, cbData) == 0))
    {
        /*
         * Process the IOCtl.
         */
        size_t cbDataReturned;
        rc = VBoxGuestCommonIOCtl(uCmd, &g_DevExt, pSession, pvBuf, cbData, &cbDataReturned);

        /*
         * Copy ioctl data and output buffer back to user space.
         */
        if (RT_SUCCESS(rc))
        {
            rc = 0;
            if (RT_UNLIKELY(cbDataReturned > cbData))
            {
                LogRel((DEVICE_NAME "::IOCtl: too much output data %u expected %u\n", cbDataReturned, cbData));
                cbDataReturned = cbData;
            }
            if (cbDataReturned > 0)
            {
                if (RT_UNLIKELY(copy_to_user((void *)ulArg, pvBuf, cbDataReturned) != 0))
                {
                    LogRel((DEVICE_NAME "::IOCtl: copy_to_user failed; pvBuf=%p ulArg=%p cbDataReturned=%u uCmd=%d\n",
                            pvBuf, (void *)ulArg, cbDataReturned, uCmd, rc));
                    rc = -EFAULT;
                }
            }
        }
        else
        {
            Log(("vboxguestLinuxIOCtl: pFilp=%p uCmd=%#x ulArg=%p failed, rc=%d\n", pFilp, uCmd, (void *)ulArg, rc));
            rc = -rc; Assert(rc > 0); /* Positive returns == negated VBox error status codes. */
        }
    }
    else
    {
        Log((DEVICE_NAME "::IOCtl: copy_from_user(,%#lx, %#x) failed; uCmd=%#x.\n", ulArg, cbData, uCmd));
        rc = -EFAULT;
    }
    if (pvBufFree)
        RTMemFree(pvBufFree);

    Log6(("vboxguestLinuxIOCtl: returns %d (pid=%d/%d)\n", rc, RTProcSelf(), current->pid));
    return rc;
}


/**
 * Asynchronous notification activation method.
 *
 * @returns 0 on success, negative errno on failure.
 *
 * @param   fd          The file descriptor.
 * @param   pFile       The file structure.
 * @param   fOn         On/off indicator.
 */
static int vboxguestFAsync(int fd, struct file *pFile, int fOn)
{
    return fasync_helper(fd, pFile, fOn, &g_pFAsyncQueue);
}


/**
 * Poll function.
 *
 * This returns ready to read if the mouse pointer mode or the pointer position
 * has changed since last call to read.
 *
 * @returns 0 if no changes, POLLIN | POLLRDNORM if there are unseen changes.
 *
 * @param   pFile       The file structure.
 * @param   pPt         The poll table.
 *
 * @remarks This is probably not really used, X11 is said to use the fasync
 *          interface instead.
 */
static unsigned int vboxguestPoll(struct file *pFile, poll_table *pPt)
{
    PVBOXGUESTSESSION   pSession  = (PVBOXGUESTSESSION)pFile->private_data;
    uint32_t            u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    unsigned int        fMask     = pSession->u32MousePosChangedSeq != u32CurSeq
                                  ? POLLIN | POLLRDNORM
                                  : 0;
    poll_wait(pFile, &g_PollEventQueue, pPt);
    return fMask;
}


/**
 * Read to go with our poll/fasync response.
 *
 * @returns 1 or -EINVAL.
 *
 * @param   pFile       The file structure.
 * @param   pbBuf       The buffer to read into.
 * @param   cbRead      The max number of bytes to read.
 * @param   poff        The current file position.
 *
 * @remarks This is probably not really used as X11 lets the driver do its own
 *          event reading. The poll condition is therefore also cleared when we
 *          see VMMDevReq_GetMouseStatus in VBoxGuestCommonIOCtl_VMMRequest.
 */
static ssize_t vboxguestRead(struct file *pFile, char *pbBuf, size_t cbRead, loff_t *poff)
{
    PVBOXGUESTSESSION   pSession  = (PVBOXGUESTSESSION)pFile->private_data;
    uint32_t            u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);

    if (*poff != 0)
        return -EINVAL;

    /*
     * Fake a single byte read if we're not up to date with the current mouse position.
     */
    if (    pSession->u32MousePosChangedSeq != u32CurSeq
        &&  cbRead > 0)
    {
        pSession->u32MousePosChangedSeq = u32CurSeq;
        pbBuf[0] = 0;
        return 1;
    }
    return 0;
}


void VBoxGuestNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    NOREF(pDevExt);

    /*
     * Wake up everyone that's in a poll() and post anyone that has
     * subscribed to async notifications.
     */
    Log(("VBoxGuestNativeISRMousePollEvent: wake_up_all\n"));
    wake_up_all(&g_PollEventQueue);
    Log(("VBoxGuestNativeISRMousePollEvent: kill_fasync\n"));
    kill_fasync(&g_pFAsyncQueue, SIGIO, POLL_IN);
    Log(("VBoxGuestNativeISRMousePollEvent: done\n"));
}


/* Common code that depend on g_DevExt. */
#include "VBoxGuestIDC-unix.c.h"

EXPORT_SYMBOL(VBoxGuestIDCOpen);
EXPORT_SYMBOL(VBoxGuestIDCClose);
EXPORT_SYMBOL(VBoxGuestIDCCall);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)

/** log and dbg_log parameter setter. */
static int vboxguestLinuxParamLogGrpSet(const char *pszValue, struct kernel_param *pParam)
{
    if (g_fLoggerCreated)
    {
        PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
        if (pLogger)
            RTLogGroupSettings(pLogger, pszValue);
    }
    else if (pParam->name[0] != 'd')
        strlcpy(&g_szLogGrp[0], pszValue, sizeof(g_szLogGrp));

    return 0;
}


/** log and dbg_log parameter getter. */
static int vboxguestLinuxParamLogGrpGet(char *pszBuf, struct kernel_param *pParam)
{
    PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
    *pszBuf = '\0';
    if (pLogger)
        RTLogGetGroupSettings(pLogger, pszBuf, _4K);
    return strlen(pszBuf);
}


/** log and dbg_log_flags parameter setter. */
static int vboxguestLinuxParamLogFlagsSet(const char *pszValue, struct kernel_param *pParam)
{
    if (g_fLoggerCreated)
    {
        PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
        if (pLogger)
            RTLogFlags(pLogger, pszValue);
    }
    else if (pParam->name[0] != 'd')
        strlcpy(&g_szLogFlags[0], pszValue, sizeof(g_szLogFlags));
    return 0;
}


/** log and dbg_log_flags parameter getter. */
static int vboxguestLinuxParamLogFlagsGet(char *pszBuf, struct kernel_param *pParam)
{
    PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
    *pszBuf = '\0';
    if (pLogger)
        RTLogGetFlags(pLogger, pszBuf, _4K);
    return strlen(pszBuf);
}


/** log and dbg_log_dest parameter setter. */
static int vboxguestLinuxParamLogDstSet(const char *pszValue, struct kernel_param *pParam)
{
    if (g_fLoggerCreated)
    {
        PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
        if (pLogger)
            RTLogDestinations(pLogger, pszValue);
    }
    else if (pParam->name[0] != 'd')
        strlcpy(&g_szLogDst[0], pszValue, sizeof(g_szLogDst));
    return 0;
}


/** log and dbg_log_dest parameter getter. */
static int vboxguestLinuxParamLogDstGet(char *pszBuf, struct kernel_param *pParam)
{
    PRTLOGGER pLogger = pParam->name[0] == 'd' ? RTLogDefaultInstance() : RTLogRelDefaultInstance();
    *pszBuf = '\0';
    if (pLogger)
        RTLogGetDestinations(pLogger, pszBuf, _4K);
    return strlen(pszBuf);
}

/*
 * Define module parameters.
 */
module_param_call(log,            vboxguestLinuxParamLogGrpSet,   vboxguestLinuxParamLogGrpGet,   NULL, 0664);
module_param_call(log_flags,      vboxguestLinuxParamLogFlagsSet, vboxguestLinuxParamLogFlagsGet, NULL, 0664);
module_param_call(log_dest,       vboxguestLinuxParamLogDstSet,   vboxguestLinuxParamLogDstGet,   NULL, 0664);
# ifdef LOG_ENABLED
module_param_call(dbg_log,        vboxguestLinuxParamLogGrpSet,   vboxguestLinuxParamLogGrpGet,   NULL, 0664);
module_param_call(dbg_log_flags,  vboxguestLinuxParamLogFlagsSet, vboxguestLinuxParamLogFlagsGet, NULL, 0664);
module_param_call(dbg_log_dest,   vboxguestLinuxParamLogDstSet,   vboxguestLinuxParamLogDstGet,   NULL, 0664);
# endif

#endif /* 2.6.0 and later */


module_init(vboxguestLinuxModInit);
module_exit(vboxguestLinuxModExit);

MODULE_AUTHOR(VBOX_VENDOR);
MODULE_DESCRIPTION(VBOX_PRODUCT " Guest Additions for Linux Module");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(VBOX_VERSION_STRING);
#endif

