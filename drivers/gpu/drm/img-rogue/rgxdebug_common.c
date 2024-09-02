/*************************************************************************/ /*!
@File
@Title          Rgx debug information
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX debugging functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "img_defs.h"
#include "rgxdefs_km.h"
#include "rgxdevice.h"
#include "osfunc.h"
#include "allocmem.h"

#include "rgxdebug_common.h"
#include "pvrversion.h"
#include "pvrsrv.h"
#include "rgx_fwif_sf.h"
#include "rgxfw_log_helper.h"
#include "fwtrace_string.h"
#include "rgxmmudefs_km.h"
#include "rgxinit.h"
#include "rgxfwutils.h"
#include "rgxfwriscv.h"
#include "rgxfwimageutils.h"
#include "fwload.h"
#include "rgx_options.h"
#include "devicemem_history_server.h"
#include "debug_common.h"
#include "info_page.h"
#include "osfunc.h"

#define MAX_FW_DESCRIPTION_LENGTH	(600U)

#define PVR_DUMP_FIRMWARE_INFO(x)														\
	PVR_DUMPDEBUG_LOG("FW info: %d.%d @ %8d (%s) build options: 0x%08x",				\
						PVRVERSION_UNPACK_MAJ((x).ui32DDKVersion),						\
						PVRVERSION_UNPACK_MIN((x).ui32DDKVersion),						\
						(x).ui32DDKBuild,												\
						((x).ui32BuildOptions & OPTIONS_DEBUG_EN) ? "debug":"release",	\
						(x).ui32BuildOptions);

#define PVR_DUMP_FIRMWARE_INFO_HDR(x)												\
	PVR_DUMPDEBUG_LOG("FW info: %d.%d @ %8d (%s) build options: 0x%08x",		\
						(x).ui16PVRVersionMajor,								\
						(x).ui16PVRVersionMinor,								\
						(x).ui32PVRVersionBuild,								\
						((x).ui32Flags & OPTIONS_DEBUG_EN) ? "debug":"release",	\
						(x).ui32Flags);

typedef struct {
	IMG_UINT16     ui16Mask;
	const IMG_CHAR *pszStr;
} RGXFWT_DEBUG_INFO_MSKSTR; /* pair of bit mask and debug info message string */

/*
 *  Array of all the Firmware Trace log IDs used to convert the trace data.
 */
typedef struct _TRACEBUF_LOG_ {
	RGXFW_LOG_SFids	eSFId;
	const IMG_CHAR	*pszName;
	const IMG_CHAR	*pszFmt;
	IMG_UINT32		ui32ArgNum;
} TRACEBUF_LOG;

static const TRACEBUF_LOG aLogDefinitions[] =
{
#define X(a, b, c, d, e) {RGXFW_LOG_CREATESFID(a,b,e), #c, d, e},
	RGXFW_LOG_SFIDLIST
#undef X
};

static const IMG_FLAGS2DESC asCswOpts2Description[] =
{
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_FAST, " Fast CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM, " Medium CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_SLOW, " Slow CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_NODELAY, " No Delay CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_MODE_RAND, " Random Csw enabled;"},
	{RGXFWIF_INICFG_CTXSWITCH_SRESET_EN, " SoftReset;"},
};

static const IMG_FLAGS2DESC asMisc2Description[] =
{
	{RGXFWIF_INICFG_POW_RASCALDUST, " Power Rascal/Dust;"},
	{RGXFWIF_INICFG_SPU_CLOCK_GATE, " SPU Clock Gating (requires Power Rascal/Dust);"},
	{RGXFWIF_INICFG_HWPERF_EN, " HwPerf EN;"},
	{RGXFWIF_INICFG_FBCDC_V3_1_EN, " FBCDCv3.1;"},
	{RGXFWIF_INICFG_CHECK_MLIST_EN, " Check MList;"},
	{RGXFWIF_INICFG_DISABLE_CLKGATING_EN, " ClockGating Off;"},
	{RGXFWIF_INICFG_REGCONFIG_EN, " Register Config;"},
	{RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY, " Assert on OOM;"},
	{RGXFWIF_INICFG_HWP_DISABLE_FILTER, " HWP Filter Off;"},
	{RGXFWIF_INICFG_DM_KILL_MODE_RAND_EN, " CDM Random kill;"},
	{RGXFWIF_INICFG_DISABLE_DM_OVERLAP, " DM Overlap Off;"},
	{RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER, " Assert on HWR;"},
	{RGXFWIF_INICFG_FABRIC_COHERENCY_ENABLED, " Coherent fabric on;"},
	{RGXFWIF_INICFG_VALIDATE_IRQ, " Validate IRQ;"},
	{RGXFWIF_INICFG_DISABLE_PDP_EN, " PDUMP Panic off;"},
	{RGXFWIF_INICFG_SPU_POWER_STATE_MASK_CHANGE_EN, " SPU Pow mask change on;"},
	{RGXFWIF_INICFG_WORKEST, " Workload Estim;"},
	{RGXFWIF_INICFG_PDVFS, " PDVFS;"},
	{RGXFWIF_INICFG_CDM_ARBITRATION_TASK_DEMAND, " CDM task demand arbitration;"},
	{RGXFWIF_INICFG_CDM_ARBITRATION_ROUND_ROBIN, " CDM round-robin arbitration;"},
	{RGXFWIF_INICFG_ISPSCHEDMODE_VER1_IPP, " ISP v1 scheduling;"},
	{RGXFWIF_INICFG_ISPSCHEDMODE_VER2_ISP, " ISP v2 scheduling;"},
	{RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER, " Validate SOC&USC timers;"},
};

static const IMG_FLAGS2DESC asFwOsCfg2Description[] =
{
	{RGXFWIF_INICFG_OS_CTXSWITCH_TDM_EN, " TDM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_GEOM_EN, " GEOM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN, " 3D;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_CDM_EN, " CDM;"},
#if defined(SUPPORT_RAY_TRACING)
	{RGXFWIF_INICFG_OS_CTXSWITCH_RDM_EN, " RDM;"},
#endif
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_TDM, " LowPrio TDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_GEOM, " LowPrio GEOM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_3D, " LowPrio 3D;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_CDM, " LowPrio CDM;"},
#if defined(SUPPORT_RAY_TRACING)
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_RDM, " LowPrio RDM;"},
#endif
};

#define NARGS_MASK ~(0xF<<16)
static IMG_BOOL _FirmwareTraceIntegrityCheck(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile)
{
	const TRACEBUF_LOG *psLogDef = &aLogDefinitions[0];
	IMG_BOOL bIntegrityOk = IMG_TRUE;

	/*
	 * For every log ID, check the format string and number of arguments is valid.
	 */
	while (psLogDef->eSFId != RGXFW_SF_LAST)
	{
		const TRACEBUF_LOG *psLogDef2;
		const IMG_CHAR *pszString;
		IMG_UINT32 ui32Count;

		/*
		 * Check the number of arguments matches the number of '%' in the string and
		 * check that no string uses %s which is not supported as it requires a
		 * pointer to memory that is not going to be valid.
		 */
		pszString = psLogDef->pszFmt;
		ui32Count = 0;

		while (*pszString != '\0')
		{
			if (*pszString++ == '%')
			{
				ui32Count++;
				if (*pszString == 's')
				{
					bIntegrityOk = IMG_FALSE;
					PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has an unsupported type not recognized (fmt: %%%c). Please fix.",
									  psLogDef->pszName, *pszString);
				}
				else if (*pszString == '%')
				{
					/* Double % is a printable % sign and not a format string... */
					ui32Count--;
				}
			}
		}

		if (ui32Count != psLogDef->ui32ArgNum)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has %d arguments but only %d are specified. Please fix.",
			                  psLogDef->pszName, ui32Count, psLogDef->ui32ArgNum);
		}

		/* RGXDumpFirmwareTrace() has a hardcoded limit of supporting up to 20 arguments... */
		if (ui32Count > 20)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has %d arguments but a maximum of 20 are supported. Please fix.",
			                  psLogDef->pszName, ui32Count);
		}

		/* Check the id number is unique (don't take into account the number of arguments) */
		ui32Count = 0;
		psLogDef2 = &aLogDefinitions[0];

		while (psLogDef2->eSFId != RGXFW_SF_LAST)
		{
			if ((psLogDef->eSFId & NARGS_MASK) == (psLogDef2->eSFId & NARGS_MASK))
			{
				ui32Count++;
			}
			psLogDef2++;
		}

		if (ui32Count != 1)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s id %x is not unique, there are %d more. Please fix.",
			                  psLogDef->pszName, psLogDef->eSFId, ui32Count - 1);
		}

		/* Move to the next log ID... */
		psLogDef++;
	}

	return bIntegrityOk;
}

void RGXDumpFirmwareTrace(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO  *psDevInfo)
{
	RGXFWIF_TRACEBUF  *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	static IMG_BOOL   bIntegrityCheckPassed = IMG_FALSE;

	/* Check that the firmware trace is correctly defined... */
	if (!bIntegrityCheckPassed)
	{
		bIntegrityCheckPassed = _FirmwareTraceIntegrityCheck(pfnDumpDebugPrintf, pvDumpDebugFile);
		if (!bIntegrityCheckPassed)
		{
			return;
		}
	}

	/* Dump FW trace information... */
	if (psRGXFWIfTraceBufCtl != NULL)
	{
		IMG_UINT32  tid;

		PVR_DUMPDEBUG_LOG("Device ID: %u", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

		RGXFwSharedMemCacheOpValue(psRGXFWIfTraceBufCtl->ui32LogType, INVALIDATE);

		/* Print the log type settings... */
		if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
		{
			PVR_DUMPDEBUG_LOG("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
							  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
							  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
							  );
		}
		else
		{
			PVR_DUMPDEBUG_LOG("Debug log type: none");
		}

		/* Print the decoded log for each thread... */
		for (tid = 0;  tid < RGXFW_THREAD_NUM;  tid++)
		{
			RGXDumpFirmwareTraceDecoded(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, psRGXFWIfTraceBufCtl, tid);
		}
	}
}

/*!
*******************************************************************************

 @Function	RGXPrepareExtraDebugInfo

 @Description

 Prepares debug info string by decoding ui16DebugInfo value passed

 @Input pszBuffer	 - pointer to debug info string buffer

 @Return   void

******************************************************************************/
static void RGXPrepareExtraDebugInfo(IMG_CHAR *pszBuffer, IMG_UINT32 ui32BufferSize, IMG_UINT16 ui16DebugInfo)
{
	const RGXFWT_DEBUG_INFO_MSKSTR aDebugInfoMskStr[] =
	{
#define X(a, b) {a, b},
		RGXFWT_DEBUG_INFO_MSKSTRLIST
#undef X
	};

	IMG_UINT32 ui32NumFields = sizeof(aDebugInfoMskStr)/sizeof(RGXFWT_DEBUG_INFO_MSKSTR);
	IMG_UINT32 i;
	IMG_BOOL   bHasExtraDebugInfo = IMG_FALSE;

	/* Add prepend string */
	OSStringLCopy(pszBuffer, RGXFWT_DEBUG_INFO_STR_PREPEND, ui32BufferSize);

	/* Add debug info strings */
	for (i = 0; i < ui32NumFields; i++)
	{
		if (ui16DebugInfo & aDebugInfoMskStr[i].ui16Mask)
		{
			if (bHasExtraDebugInfo)
			{
				OSStringLCat(pszBuffer, ", ", ui32BufferSize); /* Add comma separator */
			}
			OSStringLCat(pszBuffer, aDebugInfoMskStr[i].pszStr, ui32BufferSize);
			bHasExtraDebugInfo = IMG_TRUE;
		}
	}

	/* Add append string */
	OSStringLCat(pszBuffer, RGXFWT_DEBUG_INFO_STR_APPEND, ui32BufferSize);
}

#define PVR_MAX_DEBUG_PARTIAL_LINES (40U)
#define PVR_DUMPDEBUG_LOG_LINES(fmt, ...) \
	if (!bPrintAllLines) { \
		OSSNPrintf(&pszLineBuffer[ui32LastLineIdx * PVR_MAX_DEBUG_MESSAGE_LEN], PVR_MAX_DEBUG_MESSAGE_LEN, (fmt), ##__VA_ARGS__); \
		ui32LineCount++; \
		ui32LastLineIdx = ui32LineCount % PVR_MAX_DEBUG_PARTIAL_LINES; \
	} else { \
		PVR_UNREFERENCED_PARAMETER(pszLineBuffer); \
		PVR_UNREFERENCED_PARAMETER(ui32LineCount); \
		PVR_UNREFERENCED_PARAMETER(ui32LastLineIdx); \
		PVR_DUMPDEBUG_LOG((fmt), ##__VA_ARGS__); \
	}

static void RGXDumpFirmwareTraceLines(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl,
				IMG_UINT32 ui32TID,
				bool bPrintAllLines)
{
	volatile IMG_UINT32  *pui32FWWrapCount;
	volatile IMG_UINT32  *pui32FWTracePtr;
	IMG_UINT32           *pui32TraceBuf;
	IMG_UINT32           *pui32LocalTraceBuf = NULL;
	IMG_UINT32           ui32HostWrapCount;
	IMG_UINT32           ui32HostTracePtr;
	IMG_UINT32           ui32Count           = 0;
	IMG_UINT32           ui32LineCount       = 0;
	IMG_UINT32           ui32LastLineIdx     = 0;
	IMG_CHAR             *pszLineBuffer      = NULL;
	IMG_UINT32           ui32TraceBufSizeInDWords;

	RGXFwSharedMemCacheOpValue(psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID], INVALIDATE);

	pui32FWWrapCount = &(psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID].ui32WrapCount);
	pui32FWTracePtr  = &(psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID].ui32TracePointer);
	pui32TraceBuf    = psDevInfo->apui32TraceBuffer[ui32TID];
	ui32HostWrapCount = *pui32FWWrapCount;
	ui32HostTracePtr  = *pui32FWTracePtr;

	if (pui32TraceBuf == NULL)
	{
		/* trace buffer not yet allocated */
		return;
	}

	if (!bPrintAllLines)
	{
		pszLineBuffer = OSAllocMem(PVR_MAX_DEBUG_MESSAGE_LEN * PVR_MAX_DEBUG_PARTIAL_LINES);
		PVR_LOG_RETURN_VOID_IF_FALSE(pszLineBuffer != NULL, "pszLineBuffer alloc failed");
	}

	ui32TraceBufSizeInDWords = psDevInfo->ui32TraceBufSizeInDWords;

	if (ui32HostTracePtr >= ui32TraceBufSizeInDWords)
	{
		PVR_DUMPDEBUG_LOG_LINES("WARNING: Trace pointer (%d) greater than buffer size (%d).",
								ui32HostTracePtr, ui32TraceBufSizeInDWords);
		ui32HostTracePtr %= ui32TraceBufSizeInDWords;
	}

	/*
	 *  Allocate a local copy of the trace buffer which will contain a static non-changing
	 *  snapshot view of the buffer. This removes the issue of a fast GPU wrapping and
	 *  overwriting the tail data of the buffer.
	 */
	pui32LocalTraceBuf = OSAllocMem(ui32TraceBufSizeInDWords * sizeof(IMG_UINT32));
	if (pui32LocalTraceBuf != NULL)
	{
		memcpy(pui32LocalTraceBuf, pui32TraceBuf, ui32TraceBufSizeInDWords * sizeof(IMG_UINT32));
		ui32HostTracePtr = *pui32FWTracePtr;
		pui32TraceBuf = pui32LocalTraceBuf;
	}

	while (ui32Count < ui32TraceBufSizeInDWords)
	{
		IMG_UINT32  ui32Data, ui32DataToId;

		/* Find the first valid log ID, skipping whitespace... */
		do
		{
			IMG_UINT32 ui32ValidatedHostTracePtr;
			ui32ValidatedHostTracePtr = OSConfineArrayIndexNoSpeculation(ui32HostTracePtr,
			                                                             ui32TraceBufSizeInDWords);
			ui32Data     = pui32TraceBuf[ui32ValidatedHostTracePtr];
			ui32DataToId = idToStringID(ui32Data, SFs);

			/* If an unrecognized id is found it may be inconsistent data or a firmware trace error. */
			if (ui32DataToId == RGXFW_SF_LAST  &&  RGXFW_LOG_VALIDID(ui32Data))
			{
				PVR_DUMPDEBUG_LOG_LINES("WARNING: Unrecognized id (%x). From here on the trace might be wrong!", ui32Data);
			}

			/* Update the trace pointer... */
			ui32HostTracePtr++;
			if (ui32HostTracePtr >= ui32TraceBufSizeInDWords)
			{
				ui32HostTracePtr = 0;
				ui32HostWrapCount++;
			}
			ui32Count++;
		} while ((RGXFW_SF_LAST == ui32DataToId)  &&
				 ui32Count < ui32TraceBufSizeInDWords);

		if (ui32Count < ui32TraceBufSizeInDWords)
		{
			IMG_CHAR   szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN] = "%" IMG_UINT64_FMTSPEC ":T%u-%s> ";
			IMG_CHAR   szDebugInfoBuffer[RGXFWT_DEBUG_INFO_STR_MAXLEN] = "";
			IMG_UINT64 ui64Timestamp;
			IMG_UINT16 ui16DebugInfo;

			/* If we hit the ASSERT message then this is the end of the log... */
			if (ui32Data == RGXFW_SF_MAIN_ASSERT_FAILED)
			{
				PVR_DUMPDEBUG_LOG_LINES("ASSERTION %.*s failed at %.*s:%u",
				                        RGXFW_TRACE_BUFFER_ASSERT_SIZE,
				                        psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID].sAssertBuf.szInfo,
				                        RGXFW_TRACE_BUFFER_ASSERT_SIZE,
				                        psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID].sAssertBuf.szPath,
				                        psRGXFWIfTraceBufCtl->sTraceBuf[ui32TID].sAssertBuf.ui32LineNum);
				break;
			}

			ui64Timestamp = (IMG_UINT64)(pui32TraceBuf[(ui32HostTracePtr + 0) % ui32TraceBufSizeInDWords]) << 32 |
							(IMG_UINT64)(pui32TraceBuf[(ui32HostTracePtr + 1) % ui32TraceBufSizeInDWords]);

			ui16DebugInfo = (IMG_UINT16) ((ui64Timestamp & ~RGXFWT_TIMESTAMP_DEBUG_INFO_CLRMSK) >> RGXFWT_TIMESTAMP_DEBUG_INFO_SHIFT);
			ui64Timestamp = (ui64Timestamp & ~RGXFWT_TIMESTAMP_TIME_CLRMSK) >> RGXFWT_TIMESTAMP_TIME_SHIFT;

			/*
			 * Print the trace string and provide up to 20 arguments which
			 * printf function will be able to use. We have already checked
			 * that no string uses more than this.
			 */
			OSStringLCat(szBuffer, SFs[ui32DataToId].psName, PVR_MAX_DEBUG_MESSAGE_LEN);

			/* Check and append any extra debug info available */
			if (ui16DebugInfo)
			{
				/* Prepare debug info string */
				RGXPrepareExtraDebugInfo(szDebugInfoBuffer, RGXFWT_DEBUG_INFO_STR_MAXLEN, ui16DebugInfo);

				/* Append debug info string */
				OSStringLCat(szBuffer, szDebugInfoBuffer, PVR_MAX_DEBUG_MESSAGE_LEN);
			}

			PVR_DUMPDEBUG_LOG_LINES(szBuffer, ui64Timestamp, ui32TID, groups[RGXFW_SF_GID(ui32Data)],
			                        pui32TraceBuf[(ui32HostTracePtr +  2) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  3) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  4) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  5) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  6) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  7) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  8) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr +  9) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 10) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 11) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 12) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 13) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 14) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 15) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 16) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 17) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 18) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 19) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 20) % ui32TraceBufSizeInDWords],
			                        pui32TraceBuf[(ui32HostTracePtr + 21) % ui32TraceBufSizeInDWords]);

			/* Update the trace pointer... */
			ui32HostTracePtr = ui32HostTracePtr + 2 + RGXFW_SF_PARAMNUM(ui32Data);
			if (ui32HostTracePtr >= ui32TraceBufSizeInDWords)
			{
				ui32HostTracePtr = ui32HostTracePtr % ui32TraceBufSizeInDWords;
				ui32HostWrapCount++;
			}
			ui32Count = (ui32Count + 2 + RGXFW_SF_PARAMNUM(ui32Data));

			/* Has the FW trace buffer overtaken the host pointer during the last line printed??? */
			if ((pui32LocalTraceBuf == NULL)  &&
			    ((*pui32FWWrapCount > ui32HostWrapCount) ||
			     ((*pui32FWWrapCount == ui32HostWrapCount) && (*pui32FWTracePtr > ui32HostTracePtr))))
			{
				/* Move forward to the oldest entry again... */
				PVR_DUMPDEBUG_LOG_LINES(". . .");
				ui32HostWrapCount = *pui32FWWrapCount;
				ui32HostTracePtr  = *pui32FWTracePtr;
			}
		}
	}

	/* Free the local copy of the trace buffer if it was allocated... */
	if (pui32LocalTraceBuf != NULL)
	{
		OSFreeMem(pui32LocalTraceBuf);
	}

	if (!bPrintAllLines)
	{
		IMG_UINT32 ui32FirstLineIdx;

		if (ui32LineCount > PVR_MAX_DEBUG_PARTIAL_LINES)
		{
			ui32FirstLineIdx = ui32LastLineIdx;
			ui32LineCount = PVR_MAX_DEBUG_PARTIAL_LINES;
		}
		else
		{
			ui32FirstLineIdx = 0;
		}

		for (ui32Count = 0; ui32Count < ui32LineCount; ui32Count++)
		{
			PVR_DUMPDEBUG_LOG("%s", &pszLineBuffer[((ui32FirstLineIdx + ui32Count) % PVR_MAX_DEBUG_PARTIAL_LINES) * PVR_MAX_DEBUG_MESSAGE_LEN]);
		}

		OSFreeMem(pszLineBuffer);
	}
}

void RGXDumpFirmwareTraceDecoded(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl,
				IMG_UINT32 ui32TID)
{
	RGXDumpFirmwareTraceLines(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile,
	                          psRGXFWIfTraceBufCtl, ui32TID, true);
}

void RGXDumpFirmwareTracePartial(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl,
				IMG_UINT32 ui32TID)
{
	RGXDumpFirmwareTraceLines(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile,
	                          psRGXFWIfTraceBufCtl, ui32TID, false);
}

void RGXDumpFirmwareTraceBinary(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl,
				IMG_UINT32 ui32TID)
{
	IMG_UINT32	i;
	IMG_BOOL	bPrevLineWasZero = IMG_FALSE;
	IMG_BOOL	bLineIsAllZeros = IMG_FALSE;
	IMG_UINT32	ui32CountLines = 0;
	IMG_UINT32	*pui32TraceBuffer;
	IMG_CHAR *pszLine;

	RGXFwSharedMemCacheOpExec(psDevInfo->apui32TraceBuffer[ui32TID],
	                          psDevInfo->ui32TraceBufSizeInDWords * sizeof(IMG_UINT32),
	                          PVRSRV_CACHE_OP_INVALIDATE);
	pui32TraceBuffer = psDevInfo->apui32TraceBuffer[ui32TID];

/* Max number of DWords to be printed per line, in debug dump binary output */
#define PVR_DD_FW_TRACEBUF_LINESIZE 30U
	/* each element in the line is 8 characters plus a space.  The '+ 1' is because of the final trailing '\0'. */
	pszLine = OSAllocMem(9 * PVR_DD_FW_TRACEBUF_LINESIZE + 1);
	if (pszLine == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Out of mem allocating line string (size: %d)",
				__func__,
				9 * PVR_DD_FW_TRACEBUF_LINESIZE + 1));
		return;
	}

	for (i = 0; i < psDevInfo->ui32TraceBufSizeInDWords; i += PVR_DD_FW_TRACEBUF_LINESIZE)
	{
		IMG_UINT32 k = 0;
		IMG_UINT32 ui32Line = 0x0;
		IMG_UINT32 ui32LineOffset = i*sizeof(IMG_UINT32);
		IMG_CHAR   *pszBuf = pszLine;

		for (k = 0; k < PVR_DD_FW_TRACEBUF_LINESIZE; k++)
		{
			if ((i + k) >= psDevInfo->ui32TraceBufSizeInDWords)
			{
				/* Stop reading when the index goes beyond trace buffer size. This condition is
				 * hit during printing the last line in DD when ui32TraceBufSizeInDWords is not
				 * a multiple of PVR_DD_FW_TRACEBUF_LINESIZE */
				break;
			}

			ui32Line |= pui32TraceBuffer[i + k];

			/* prepare the line to print it. The '+1' is because of the trailing '\0' added */
			OSSNPrintf(pszBuf, 9 + 1, " %08x", pui32TraceBuffer[i + k]);
			pszBuf += 9; /* write over the '\0' */
		}

		bLineIsAllZeros = (ui32Line == 0x0);

		if (bLineIsAllZeros)
		{
			if (bPrevLineWasZero)
			{
				ui32CountLines++;
			}
			else
			{
				bPrevLineWasZero = IMG_TRUE;
				ui32CountLines = 1;
				PVR_DUMPDEBUG_LOG("FWT[%08x]: 00000000 ... 00000000", ui32LineOffset);
			}
		}
		else
		{
			if (bPrevLineWasZero  &&  ui32CountLines > 1)
			{
				PVR_DUMPDEBUG_LOG("FWT[...]: %d lines were all zero", ui32CountLines);
			}
			bPrevLineWasZero = IMG_FALSE;

			PVR_DUMPDEBUG_LOG("FWT[%08x]:%s", ui32LineOffset, pszLine);
		}
	}

	if (bPrevLineWasZero)
	{
		PVR_DUMPDEBUG_LOG("FWT[END]: %d lines were all zero", ui32CountLines);
	}

	OSFreeMem(pszLine);
}

void RGXDocumentFwMapping(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				const IMG_UINT32 ui32FwVA,
				const IMG_CPU_PHYADDR sCpuPA,
				const IMG_DEV_PHYADDR sDevPA,
				const IMG_UINT64 ui64PTE)
{
#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
	{
		PVR_DUMPDEBUG_LOG("|    0x%08X   |   "
						  "0x%016" IMG_UINT64_FMTSPECX "   |   "
						  "0x%016" IMG_UINT64_FMTSPECX "   |   "
						  "%s%s%s   |",
						  ui32FwVA,
						  (IMG_UINT64) sCpuPA.uiAddr,
						  sDevPA.uiAddr,
						  gapszMipsPermissionPTFlags[RGXMIPSFW_TLB_GET_INHIBIT(ui64PTE)],
						  gapszMipsDirtyGlobalValidPTFlags[RGXMIPSFW_TLB_GET_DGV(ui64PTE)],
						  gapszMipsCoherencyPTFlags[RGXMIPSFW_TLB_GET_COHERENCY(ui64PTE)]);
	}
	else
#endif
	{
		const char *pszSLCBypass =
#if defined(RGX_MMUCTRL_PT_DATA_SLC_BYPASS_CTRL_EN)
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_SLC_BYPASS_CTRL_EN) ? "B" : " ";
#else
						  " ";
#endif

		/* META and RISCV use a subset of the GPU's virtual address space */
		PVR_DUMPDEBUG_LOG("|    0x%08X   |   "
						  "0x%016" IMG_UINT64_FMTSPECX "   |   "
						  "0x%016" IMG_UINT64_FMTSPECX "   |   "
						  "%s%s%s%s%s%s   |",
						  ui32FwVA,
						  (IMG_UINT64) sCpuPA.uiAddr,
						  sDevPA.uiAddr,
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_ENTRY_PENDING_EN)   ? "P" : " ",
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_PM_SRC_EN)          ? "PM" : "  ",
						  pszSLCBypass,
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_CC_EN)              ? "C" : " ",
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_READ_ONLY_EN)       ? "RO" : "RW",
						  BITMASK_HAS(ui64PTE, RGX_MMUCTRL_PT_DATA_VALID_EN)           ? "V" : " ");
	}
}


#if !defined(NO_HARDWARE)
static PVRSRV_ERROR
RGXPollMetaRegThroughSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32RegOffset,
                        IMG_UINT32 ui32PollValue, IMG_UINT32 ui32Mask)
{
	IMG_UINT32 ui32RegValue, ui32NumPolls = 0;
	PVRSRV_ERROR eError;

	do
	{
		eError = RGXReadFWModuleAddr(psDevInfo, ui32RegOffset, &ui32RegValue);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	} while (((ui32RegValue & ui32Mask) != ui32PollValue) && (ui32NumPolls++ < 1000));

	return ((ui32RegValue & ui32Mask) == ui32PollValue) ? PVRSRV_OK : PVRSRV_ERROR_RETRY;
}

PVRSRV_ERROR
RGXReadMetaCoreReg(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32RegAddr, IMG_UINT32 *pui32RegVal)
{
	PVRSRV_ERROR eError;

	/* Core Read Ready? */
	eError = RGXPollMetaRegThroughSP(psDevInfo,
	                                 META_CR_TXUXXRXRQ_OFFSET,
	                                 META_CR_TXUXXRXRQ_DREADY_BIT,
									 META_CR_TXUXXRXRQ_DREADY_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXPollMetaRegThroughSP");

	/* Set the reg we are interested in reading */
	eError = RGXWriteFWModuleAddr(psDevInfo, META_CR_TXUXXRXRQ_OFFSET,
	                        ui32RegAddr | META_CR_TXUXXRXRQ_RDnWR_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXWriteFWModuleAddr");

	/* Core Read Done? */
	eError = RGXPollMetaRegThroughSP(psDevInfo,
	                                 META_CR_TXUXXRXRQ_OFFSET,
	                                 META_CR_TXUXXRXRQ_DREADY_BIT,
									 META_CR_TXUXXRXRQ_DREADY_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXPollMetaRegThroughSP");

	/* Read the value */
	return RGXReadFWModuleAddr(psDevInfo, META_CR_TXUXXRXDT_OFFSET, pui32RegVal);
}
#endif /* !defined(NO_HARDWARE) */

#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
static PVRSRV_ERROR _ValidateWithFWModule(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo,
						RGXFWIF_DEV_VIRTADDR *psFWAddr,
						void *pvHostCodeAddr,
						IMG_UINT32 ui32MaxLen,
						const IMG_CHAR *pszDesc,
						IMG_UINT32 ui32StartOffset)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32Value = 0;
	IMG_UINT32 ui32FWCodeDevVAAddr = psFWAddr->ui32Addr + ui32StartOffset;
	IMG_UINT32 *pui32FWCode = (IMG_UINT32*) IMG_OFFSET_ADDR(pvHostCodeAddr,ui32StartOffset);
	IMG_UINT32 i;

#if defined(EMULATOR)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		return PVRSRV_OK;
	}
#endif

	ui32MaxLen -= ui32StartOffset;
	ui32MaxLen /= sizeof(IMG_UINT32); /* Byte -> 32 bit words */

	for (i = 0; i < ui32MaxLen; i++)
	{
		eError = RGXReadFWModuleAddr(psDevInfo, ui32FWCodeDevVAAddr, &ui32Value);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			return eError;
		}

#if defined(EMULATOR)
		if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
#endif
		{
			PVR_DPF((PVR_DBG_VERBOSE, "0x%x: CPU 0x%08x, FW 0x%08x", i * 4, pui32FWCode[i], ui32Value));

			if (pui32FWCode[i] != ui32Value)
			{
				PVR_DUMPDEBUG_LOG("%s: Mismatch while validating %s at offset 0x%x: CPU 0x%08x (%p), FW 0x%08x (%x)",
					 __func__, pszDesc,
					 (i * 4) + ui32StartOffset, pui32FWCode[i], pui32FWCode, ui32Value, ui32FWCodeDevVAAddr);
				return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
			}
		}

		ui32FWCodeDevVAAddr += 4;
	}

	PVR_DUMPDEBUG_LOG("Match between Host and Firmware view of the %s", pszDesc);
	return PVRSRV_OK;
}
#endif

#if !defined(NO_HARDWARE)
PVRSRV_ERROR RGXValidateFWImage(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(SUPPORT_TRUSTED_DEVICE)
	PVRSRV_ERROR eError;
	IMG_UINT32 *pui32HostFWCode = NULL, *pui32HostFWCoremem = NULL;
	OS_FW_IMAGE *psRGXFW = NULL;
	const IMG_BYTE *pbRGXFirmware = NULL;
#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	IMG_UINT32 *pui32CodeMemoryPointer;
#endif
	RGXFWIF_DEV_VIRTADDR sFWAddr;
	IMG_UINT32 ui32StartOffset = 0;
	RGX_LAYER_PARAMS sLayerParams;
	sLayerParams.psDevInfo = psDevInfo;

#if defined(EMULATOR)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		PVR_DUMPDEBUG_LOG("Validation of RISC-V FW code is disabled on emulator");
		return PVRSRV_OK;
	}
#endif

	if (psDevInfo->pvRegsBaseKM == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: RGX registers not mapped yet!", __func__));
		return PVRSRV_ERROR_BAD_MAPPING;
	}

	/* Load FW from system for code verification */
	pui32HostFWCode = OSAllocZMem(psDevInfo->ui32FWCodeSizeInBytes);
	if (pui32HostFWCode == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed in allocating memory for FW code. "
				"So skipping FW code verification",
				__func__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Coremem is not present on all GPU cores, so size can be zero */
	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		pui32HostFWCoremem = OSAllocZMem(psDevInfo->ui32FWCorememCodeSizeInBytes);
		if (pui32HostFWCoremem == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed in allocating memory for FW core code. "
					"So skipping FW code verification",
					__func__));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto freeHostFWCode;
		}
	}

	/* Load FW image */
	eError = RGXLoadAndGetFWData(psDevInfo->psDeviceNode, &psRGXFW, &pbRGXFirmware);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to load FW image file (%s).",
		         __func__, PVRSRVGetErrorString(eError)));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto cleanup_initfw;
	}

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		eError = ProcessLDRCommandStream(&sLayerParams, pbRGXFirmware,
						(void*) pui32HostFWCode, NULL,
						(void*) pui32HostFWCoremem, NULL, NULL);
	}
#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	else if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
	{
		eError = ProcessELFCommandStream(&sLayerParams, pbRGXFirmware,
		                                 pui32HostFWCode, NULL,
		                                 NULL, NULL);
	}
#endif
	else if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		eError = ProcessELFCommandStream(&sLayerParams, pbRGXFirmware,
		                                 pui32HostFWCode, NULL,
		                                 pui32HostFWCoremem, NULL);
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed in parsing FW image file.", __func__));
		goto cleanup_initfw;
	}

#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, (void **)&pui32CodeMemoryPointer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Error in acquiring MIPS FW code memory area (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
			goto cleanup_initfw;
		}

		RGXFwSharedMemCacheOpExec(pui32CodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes, PVRSRV_CACHE_OP_INVALIDATE);

		if (OSMemCmp(pui32HostFWCode, pui32CodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes) == 0)
		{
			PVR_DUMPDEBUG_LOG("Match between Host and MIPS views of the FW code" );
		}
		else
		{
			IMG_UINT32 ui32Count = 10; /* Show only the first 10 mismatches */
			IMG_UINT32 ui32Offset;

			PVR_DUMPDEBUG_LOG("Mismatch between Host and MIPS views of the FW code");
			for (ui32Offset = 0; (ui32Offset*4 < psDevInfo->ui32FWCodeSizeInBytes) || (ui32Count == 0); ui32Offset++)
			{
				if (pui32HostFWCode[ui32Offset] != pui32CodeMemoryPointer[ui32Offset])
				{
					PVR_DUMPDEBUG_LOG("At %d bytes, code should be 0x%x but it is instead 0x%x",
					   ui32Offset*4, pui32HostFWCode[ui32Offset], pui32CodeMemoryPointer[ui32Offset]);
					ui32Count--;
				}
			}
		}

		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);
	}
	else
#endif
	{
		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
		{
			/* starting checking after BOOT LOADER config */
			sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;

			ui32StartOffset = RGXFW_MAX_BOOTLDR_OFFSET;
		}
		else
		{
#if defined(RGX_FEATURE_HOST_SECURITY_VERSION_MAX_VALUE_IDX)
			/* Use bootloader code remap which is always configured before the FW is started */
			if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) >= 4)
			{
				sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_REMAP_SECURE;
			}
			else
#endif
			{
				sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_REMAP;
			}
		}

		eError = _ValidateWithFWModule(pfnDumpDebugPrintf, pvDumpDebugFile,
						psDevInfo, &sFWAddr,
						pui32HostFWCode, psDevInfo->ui32FWCodeSizeInBytes,
						"FW code", ui32StartOffset);
		if (eError != PVRSRV_OK)
		{
			goto cleanup_initfw;
		}

		/* Coremem is not present on all GPU cores, so may not be alloc'd */
		if (pui32HostFWCoremem != NULL) // && psDevInfo->ui32FWCorememCodeSizeInBytes
		{
			if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
			{
				sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);
			}
			else
			{
				sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, RISCV_COREMEM_CODE);

				/* Core must be halted while issuing abstract commands */
				eError = RGXRiscvHalt(psDevInfo);
				PVR_GOTO_IF_ERROR(eError, cleanup_initfw);
			}

			eError = _ValidateWithFWModule(pfnDumpDebugPrintf, pvDumpDebugFile,
							psDevInfo, &sFWAddr,
							pui32HostFWCoremem, psDevInfo->ui32FWCorememCodeSizeInBytes,
							"FW coremem code", 0);

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
			{
				eError = RGXRiscvResume(psDevInfo);
				PVR_GOTO_IF_ERROR(eError, cleanup_initfw);
			}
		}
	}

cleanup_initfw:
	if (psRGXFW)
	{
		OSUnloadFirmware(psRGXFW);
	}

	if (pui32HostFWCoremem)
	{
		OSFreeMem(pui32HostFWCoremem);
	}
freeHostFWCode:
	if (pui32HostFWCode)
	{
		OSFreeMem(pui32HostFWCode);
	}
	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	return PVRSRV_OK;
#endif
}
#endif /* !defined(NO_HARDWARE) */

#if defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
PVRSRV_ERROR ValidateFWOnLoad(PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
	IMG_PBYTE pbCodeMemoryPointer;
	PVRSRV_ERROR eError;
	RGXFWIF_DEV_VIRTADDR sFWAddr;

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, (void **)&pbCodeMemoryPointer);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	RGXFwSharedMemCacheOpExec(pbCodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes, PVRSRV_CACHE_OP_INVALIDATE);

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;
	}
	else
	{
		PVR_ASSERT(RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR));

#if defined(RGX_FEATURE_HOST_SECURITY_VERSION_MAX_VALUE_IDX)
		if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) >= 4)
		{
			sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_REMAP_SECURE;
		}
		else
#endif
		{
			sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_REMAP;
		}
	};

	eError = _ValidateWithFWModule(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes, "FW code", 0);
	if (eError != PVRSRV_OK)
	{
		goto releaseFWCodeMapping;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc, (void **)&pbCodeMemoryPointer);
		if (eError != PVRSRV_OK)
		{
			goto releaseFWCoreCodeMapping;
		}

		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
		{
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);
		}
		else
		{
			PVR_ASSERT(RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR));
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, RISCV_COREMEM_CODE);
		}

		eError = _ValidateWithFWModule(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer,
						psDevInfo->ui32FWCorememCodeSizeInBytes, "FW coremem code", 0);
	}

releaseFWCoreCodeMapping:
	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc);
	}
releaseFWCodeMapping:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	return PVRSRV_OK;
#endif
}
#endif

static const IMG_CHAR* _RGXGetDebugDevPowerStateString(PVRSRV_DEV_POWER_STATE ePowerState)
{
	switch (ePowerState)
	{
		case PVRSRV_DEV_POWER_STATE_DEFAULT: return "DEFAULT";
		case PVRSRV_DEV_POWER_STATE_OFF: return "OFF";
		case PVRSRV_DEV_POWER_STATE_ON: return "ON";
		default: return "UNKNOWN";
	}
}

/*
	Writes flags strings to an uninitialised buffer.
*/
static void _GetFwSysFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR szCswLabel[] = "Ctx switch options:";
	size_t uLabelLen = sizeof(szCswLabel) - 1;
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, szCswLabel, ui32DescSize);

	DebugCommonFlagStrings(psDesc, uiBytesPerDesc + uLabelLen, asCswOpts2Description, ARRAY_SIZE(asCswOpts2Description), ui32RawFlags);
	DebugCommonFlagStrings(psDesc, ui32DescSize, asMisc2Description, ARRAY_SIZE(asMisc2Description), ui32RawFlags);
}

static void _GetFwOsFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR szCswLabel[] = "Ctx switch:";
	size_t uLabelLen = sizeof(szCswLabel) - 1;
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, szCswLabel, ui32DescSize);

	DebugCommonFlagStrings(psDesc, uiBytesPerDesc + uLabelLen, asFwOsCfg2Description, ARRAY_SIZE(asFwOsCfg2Description), ui32RawFlags);
}


typedef enum _DEVICEMEM_HISTORY_QUERY_INDEX_
{
	DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING,
	DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED,
	DEVICEMEM_HISTORY_QUERY_INDEX_NEXT,
	DEVICEMEM_HISTORY_QUERY_INDEX_COUNT,
} DEVICEMEM_HISTORY_QUERY_INDEX;


/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryResult

 @Description

 Print details of a single result from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input pvDumpDebugFile          - Optional file identifier to be passed to the
                                   'printf' function if required
 @Input psFaultProcessInfo       - The process info derived from the page fault
 @Input psResult                 - The DevicememHistory result to be printed
 @Input ui32Index                - The index of the result

 @Return   void

******************************************************************************/
static void _PrintDevicememHistoryQueryResult(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						RGXMEM_PROCESS_INFO *psFaultProcessInfo,
						DEVICEMEM_HISTORY_QUERY_OUT_RESULT *psResult,
						IMG_UINT32 ui32Index,
						const IMG_CHAR* pszIndent)
{
	IMG_UINT32 ui32Remainder;
	IMG_UINT64 ui64Seconds, ui64Nanoseconds;

	RGXConvertOSTimestampToSAndNS(psResult->ui64When,
							&ui64Seconds,
							&ui64Nanoseconds);

	if (psFaultProcessInfo->uiPID != RGXMEM_SERVER_PID_FIRMWARE)
	{
		PVR_DUMPDEBUG_LOG("%s    [%u] Name: %s Base address: " IMG_DEV_VIRTADDR_FMTSPEC
					" Size: " IMG_DEVMEM_SIZE_FMTSPEC
					" Operation: %s Modified: %" IMG_UINT64_FMTSPEC
					" us ago (OS time %" IMG_UINT64_FMTSPEC
					".%09" IMG_UINT64_FMTSPEC " s)",
						pszIndent,
						ui32Index,
						psResult->szString,
						psResult->sBaseDevVAddr.uiAddr,
						psResult->uiSize,
						psResult->bMap ? "Map": "Unmap",
						OSDivide64r64(psResult->ui64Age, 1000, &ui32Remainder),
						ui64Seconds,
						ui64Nanoseconds);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s    [%u] Name: %s Base address: " IMG_DEV_VIRTADDR_FMTSPEC
					" Size: " IMG_DEVMEM_SIZE_FMTSPEC
					" Operation: %s Modified: %" IMG_UINT64_FMTSPEC
					" us ago (OS time %" IMG_UINT64_FMTSPEC
					".%09" IMG_UINT64_FMTSPEC
					") PID: %u (%s)",
						pszIndent,
						ui32Index,
						psResult->szString,
						psResult->sBaseDevVAddr.uiAddr,
						psResult->uiSize,
						psResult->bMap ? "Map": "Unmap",
						OSDivide64r64(psResult->ui64Age, 1000, &ui32Remainder),
						ui64Seconds,
						ui64Nanoseconds,
						psResult->sProcessInfo.uiPID,
						psResult->sProcessInfo.szProcessName);
	}

	if (!psResult->bRange)
	{
		PVR_DUMPDEBUG_LOG("%s        Whole allocation was %s", pszIndent, psResult->bMap ? "mapped": "unmapped");
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s        Pages %u to %u (" IMG_DEV_VIRTADDR_FMTSPEC "-" IMG_DEV_VIRTADDR_FMTSPEC ") %s%s",
										pszIndent,
										psResult->ui32StartPage,
										psResult->ui32StartPage + psResult->ui32PageCount - 1,
										psResult->sMapStartAddr.uiAddr,
										psResult->sMapEndAddr.uiAddr,
										psResult->bAll ? "(whole allocation) " : "",
										psResult->bMap ? "mapped": "unmapped");
	}
}

/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryOut

 @Description

 Print details of all the results from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input pvDumpDebugFile          - Optional file identifier to be passed to the
                                   'printf' function if required
 @Input psFaultProcessInfo       - The process info derived from the page fault
 @Input psQueryOut               - Storage for the query results

 @Return   void

******************************************************************************/
static void _PrintDevicememHistoryQueryOut(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						RGXMEM_PROCESS_INFO *psFaultProcessInfo,
						DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut,
						const IMG_CHAR* pszIndent)
{
	IMG_UINT32 i;

	if (psQueryOut->ui32NumResults == 0)
	{
		PVR_DUMPDEBUG_LOG("%s    No results", pszIndent);
	}
	else
	{
		for (i = 0; i < psQueryOut->ui32NumResults; i++)
		{
			_PrintDevicememHistoryQueryResult(pfnDumpDebugPrintf, pvDumpDebugFile,
									psFaultProcessInfo,
									&psQueryOut->sResults[i],
									i,
									pszIndent);
		}
	}
}

/* table of HW page size values and the equivalent */
static const unsigned int aui32HWPageSizeTable[][2] =
{
	{ 0, PVRSRV_4K_PAGE_SIZE },
	{ 1, PVRSRV_16K_PAGE_SIZE },
	{ 2, PVRSRV_64K_PAGE_SIZE },
	{ 3, PVRSRV_256K_PAGE_SIZE },
	{ 4, PVRSRV_1M_PAGE_SIZE },
	{ 5, PVRSRV_2M_PAGE_SIZE }
};

/*!
*******************************************************************************

 @Function	_PageSizeHWToBytes

 @Description

 Convert a HW page size value to its size in bytes

 @Input ui32PageSizeHW     - The HW page size value

 @Return   IMG_UINT32      The page size in bytes

******************************************************************************/
static IMG_UINT32 _PageSizeHWToBytes(IMG_UINT32 ui32PageSizeHW)
{
	if (ui32PageSizeHW > 5)
	{
		/* This is invalid, so return a default value as we cannot ASSERT in this code! */
		return PVRSRV_4K_PAGE_SIZE;
	}

	return aui32HWPageSizeTable[ui32PageSizeHW][1];
}

/*!
*******************************************************************************

 @Function	_GetDevicememHistoryData

 @Description

 Get the DevicememHistory results for the given PID and faulting device virtual address.
 The function will query DevicememHistory for information about the faulting page, as well
 as the page before and after.

 @Input psDeviceNode       - The device which this allocation search should be made on
 @Input uiPID              - The process ID to search for allocations belonging to
 @Input sFaultDevVAddr     - The device address to search for allocations at/before/after
 @Input asQueryOut         - Storage for the query results
 @Input ui32PageSizeBytes  - Faulted page size in bytes

 @Return IMG_BOOL          - IMG_TRUE if any results were found for this page fault

******************************************************************************/
static IMG_BOOL _GetDevicememHistoryData(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_PID uiPID,
							IMG_DEV_VIRTADDR sFaultDevVAddr,
							DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT],
							IMG_UINT32 ui32PageSizeBytes)
{
	DEVICEMEM_HISTORY_QUERY_IN sQueryIn;
	IMG_BOOL bAnyHits = IMG_FALSE;

	/* if the page fault originated in the firmware then the allocation may
	 * appear to belong to any PID, because FW allocations are attributed
	 * to the client process creating the allocation, so instruct the
	 * devicemem_history query to search all available PIDs
	 */
	if (uiPID == RGXMEM_SERVER_PID_FIRMWARE)
	{
		sQueryIn.uiPID = DEVICEMEM_HISTORY_PID_ANY;
	}
	else
	{
		sQueryIn.uiPID = uiPID;
	}

	sQueryIn.psDevNode = psDeviceNode;
	/* Query the DevicememHistory for all allocations in the previous page... */
	sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) - ui32PageSizeBytes;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING],
	                          ui32PageSizeBytes, IMG_TRUE))
	{
		bAnyHits = IMG_TRUE;
	}

	/* Query the DevicememHistory for any record at the exact address... */
	sQueryIn.sDevVAddr = sFaultDevVAddr;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED],
	                          ui32PageSizeBytes, IMG_FALSE))
	{
		bAnyHits = IMG_TRUE;
	}
	else
	{
		/* If not matched then try matching any record in the faulting page... */
		if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED],
		                          ui32PageSizeBytes, IMG_TRUE))
		{
			bAnyHits = IMG_TRUE;
		}
	}

	/* Query the DevicememHistory for all allocations in the next page... */
	sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) + ui32PageSizeBytes;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_NEXT],
	                          ui32PageSizeBytes, IMG_TRUE))
	{
		bAnyHits = IMG_TRUE;
	}

	return bAnyHits;
}

/* stored data about one page fault */
typedef struct _FAULT_INFO_
{
	/* the process info of the memory context that page faulted */
	RGXMEM_PROCESS_INFO sProcessInfo;
	IMG_DEV_VIRTADDR sFaultDevVAddr;
	MMU_FAULT_DATA   sMMUFaultData;
	DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT];
	/* the CR timer value at the time of the fault, recorded by the FW.
	 * used to differentiate different page faults
	 */
	IMG_UINT64 ui64CRTimer;
	/* time when this FAULT_INFO entry was added. used for timing
	 * reference against the map/unmap information
	 */
	IMG_UINT64 ui64When;
	IMG_UINT32 ui32FaultInfoFlags;
} FAULT_INFO;

/* history list of page faults.
 * Keeps the first `n` page faults and the last `n` page faults, like the FW
 * HWR log
 */
typedef struct _FAULT_INFO_LOG_
{
	IMG_UINT32 ui32Head;
	/* the number of faults in this log need not correspond exactly to
	 * the HWINFO number of the FW, as the FW HWINFO log may contain
	 * non-page fault HWRs
	 */
	FAULT_INFO asFaults[RGXFWIF_HWINFO_MAX];
} FAULT_INFO_LOG;

#define FAULT_INFO_PROC_INFO   (0x1U)
#define FAULT_INFO_DEVMEM_HIST (0x2U)

static FAULT_INFO_LOG gsFaultInfoLog = { 0 };

static void _FillAppForFWFaults(PVRSRV_RGXDEV_INFO *psDevInfo,
							FAULT_INFO *psInfo)
{
	IMG_UINT32 i, j;

	for (i = 0; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
	{
		for (j = 0; j < DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS; j++)
		{
			IMG_BOOL bFound;

			RGXMEM_PROCESS_INFO *psProcInfo = &psInfo->asQueryOut[i].sResults[j].sProcessInfo;
			if (!psProcInfo)
			{
				bFound = RGXPCPIDToProcessInfo(psDevInfo,
									psProcInfo->uiPID,
									psProcInfo);
				if (!bFound)
				{
					OSStringLCopy(psProcInfo->szProcessName,
									"(unknown)",
									sizeof(psProcInfo->szProcessName));
				}
			}
		}
	}
}

/*!
*******************************************************************************

 @Function	_PrintFaultInfo

 @Description

 Print all the details of a page fault from a FAULT_INFO structure

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psInfo               - The page fault occurrence to print

 @Return   void

******************************************************************************/
static void _PrintFaultInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					PVRSRV_DEVICE_NODE *psDevNode,
					void *pvDumpDebugFile,
					FAULT_INFO *psInfo,
					const IMG_CHAR* pszIndent)
{
	IMG_UINT32 i;
	IMG_UINT64 ui64Seconds, ui64Nanoseconds;

	RGXConvertOSTimestampToSAndNS(psInfo->ui64When, &ui64Seconds, &ui64Nanoseconds);

	if (BITMASK_HAS(psInfo->ui32FaultInfoFlags, FAULT_INFO_PROC_INFO))
	{
		IMG_PID uiPID = (psInfo->sProcessInfo.uiPID == RGXMEM_SERVER_PID_FIRMWARE || psInfo->sProcessInfo.uiPID == RGXMEM_SERVER_PID_PM) ?
							0 : psInfo->sProcessInfo.uiPID;

		PVR_DUMPDEBUG_LOG("%sDevice memory history for page fault address " IMG_DEV_VIRTADDR_FMTSPEC
							", CRTimer: 0x%016" IMG_UINT64_FMTSPECX
							", PID: %u (%s, unregistered: %u) OS time: "
							"%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC,
					pszIndent,
					psInfo->sFaultDevVAddr.uiAddr,
					psInfo->ui64CRTimer,
					uiPID,
					psInfo->sProcessInfo.szProcessName,
					psInfo->sProcessInfo.bUnregistered,
					ui64Seconds,
					ui64Nanoseconds);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%sCould not find PID for device memory history on PC of the fault", pszIndent);
	}

	if (BITMASK_HAS(psInfo->ui32FaultInfoFlags, FAULT_INFO_DEVMEM_HIST))
	{
		for (i = DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
		{
			const IMG_CHAR *pszWhich = NULL;

			switch (i)
			{
				case DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING:
					pszWhich = "Preceding page";
					break;
				case DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED:
					pszWhich = "Faulted page";
					break;
				case DEVICEMEM_HISTORY_QUERY_INDEX_NEXT:
					pszWhich = "Next page";
					break;
			}

			PVR_DUMPDEBUG_LOG("%s  %s:", pszIndent, pszWhich);
			_PrintDevicememHistoryQueryOut(pfnDumpDebugPrintf, pvDumpDebugFile,
								&psInfo->sProcessInfo,
								&psInfo->asQueryOut[i],
								pszIndent);
		}
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s  No matching Devmem History for fault address", pszIndent);
		DevicememHistoryDumpRecordStats(psDevNode, pfnDumpDebugPrintf, pvDumpDebugFile);
		PVR_DUMPDEBUG_LOG("%s  Records Searched -"
		                  " PP:%"IMG_UINT64_FMTSPEC
		                  " FP:%"IMG_UINT64_FMTSPEC
		                  " NP:%"IMG_UINT64_FMTSPEC,
		                  pszIndent,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING].ui64SearchCount,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED].ui64SearchCount,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_NEXT].ui64SearchCount);
	}
}

static void _RecordFaultInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
					FAULT_INFO *psInfo,
					IMG_DEV_VIRTADDR sFaultDevVAddr,
					IMG_DEV_PHYADDR sPCDevPAddr,
					IMG_UINT64 ui64CRTimer,
					IMG_UINT32 ui32PageSizeBytes)
{
	IMG_BOOL bFound = IMG_FALSE, bIsPMFault = IMG_FALSE;
	RGXMEM_PROCESS_INFO sProcessInfo;

	psInfo->ui32FaultInfoFlags = 0;
	psInfo->sFaultDevVAddr = sFaultDevVAddr;
	psInfo->ui64CRTimer = ui64CRTimer;
	psInfo->ui64When = OSClockns64();

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED)
	{
		/* Check if this is PM fault */
		if (psInfo->sMMUFaultData.eType == MMU_FAULT_TYPE_PM)
		{
			bIsPMFault = IMG_TRUE;
			bFound = IMG_TRUE;
			sProcessInfo.uiPID = RGXMEM_SERVER_PID_PM;
			OSStringLCopy(sProcessInfo.szProcessName, "PM", sizeof(sProcessInfo.szProcessName));
			sProcessInfo.szProcessName[sizeof(sProcessInfo.szProcessName) - 1] = '\0';
			sProcessInfo.bUnregistered = IMG_FALSE;
		}
		else
		{
			/* look up the process details for the faulting page catalogue */
			bFound = RGXPCAddrToProcessInfo(psDevInfo, sPCDevPAddr, &sProcessInfo);
		}

		if (bFound)
		{
			IMG_BOOL bHits;

			psInfo->ui32FaultInfoFlags = FAULT_INFO_PROC_INFO;
			psInfo->sProcessInfo = sProcessInfo;

			if (bIsPMFault)
			{
				bHits = IMG_TRUE;
			}
			else
			{
				/* get any DevicememHistory data for the faulting address */
				bHits = _GetDevicememHistoryData(psDevInfo->psDeviceNode,
								 sProcessInfo.uiPID,
								 sFaultDevVAddr,
								 psInfo->asQueryOut,
								 ui32PageSizeBytes);

				if (bHits)
				{
					psInfo->ui32FaultInfoFlags |= FAULT_INFO_DEVMEM_HIST;

					/* if the page fault was caused by the firmware then get information about
					 * which client application created the related allocations.
					 *
					 * Fill in the process info data for each query result.
					 */

					if (sProcessInfo.uiPID == RGXMEM_SERVER_PID_FIRMWARE)
					{
						_FillAppForFWFaults(psDevInfo, psInfo);
					}
				}
			}
		}
	}
}

void RGXDumpFaultAddressHostView(MMU_FAULT_DATA *psFaultData,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					const IMG_CHAR* pszIndent)
{
	MMU_LEVEL eTopLevel;
	const IMG_CHAR szPageLevel[][4] = {"", "PTE", "PDE", "PCE" };
	const IMG_CHAR szPageError[][3] = {"", "PT",  "PD",  "PC"  };

	eTopLevel = psFaultData->eTopLevel;

	if (psFaultData->eType == MMU_FAULT_TYPE_UNKNOWN)
	{
		PVR_DUMPDEBUG_LOG("%sNo live host MMU data available", pszIndent);
		return;
	}
	else if (psFaultData->eType == MMU_FAULT_TYPE_PM)
	{
		PVR_DUMPDEBUG_LOG("%sPM faulted at PC address = 0x%016" IMG_UINT64_FMTSPECx, pszIndent, psFaultData->sLevelData[MMU_LEVEL_0].ui64Address);
	}
	else
	{
		MMU_LEVEL eCurrLevel;
		PVR_ASSERT(eTopLevel < MMU_LEVEL_LAST);

		for (eCurrLevel = eTopLevel; eCurrLevel > MMU_LEVEL_0; eCurrLevel--)
		{
			MMU_LEVEL_DATA *psMMULevelData = &psFaultData->sLevelData[eCurrLevel];
			if (psMMULevelData->ui64Address)
			{
				if (psMMULevelData->uiBytesPerEntry == 4)
				{
					PVR_DUMPDEBUG_LOG("%s%s for index %d = 0x%08x and is %s",
								pszIndent,
								szPageLevel[eCurrLevel],
								psMMULevelData->ui32Index,
								(IMG_UINT) psMMULevelData->ui64Address,
								psMMULevelData->psDebugStr);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("%s%s for index %d = 0x%016" IMG_UINT64_FMTSPECx " and is %s",
								pszIndent,
								szPageLevel[eCurrLevel],
								psMMULevelData->ui32Index,
								psMMULevelData->ui64Address,
								psMMULevelData->psDebugStr);
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG("%s%s index (%d) out of bounds (%d)",
							pszIndent,
							szPageError[eCurrLevel],
							psMMULevelData->ui32Index,
							psMMULevelData->ui32NumOfEntries);
				break;
			}
		}
	}

}

void RGXDumpFaultInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                      DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                      void *pvDumpDebugFile,
					  const RGX_HWRINFO *psHWRInfo,
                      IMG_UINT32 ui32ReadIndex,
                      IMG_DEV_VIRTADDR *psFaultDevVAddr,
                      IMG_DEV_PHYADDR *psPCDevPAddr,
                      bool bPMFault,
                      IMG_UINT32 ui32PageSize)
{
	FAULT_INFO *psInfo;

	OSLockAcquire(psDevInfo->hDebugFaultInfoLock);

	/* Find the matching Fault Info for this HWRInfo */
	psInfo = &gsFaultInfoLog.asFaults[ui32ReadIndex];

	/* if they do not match, we need to update the psInfo */
	if ((psInfo->ui64CRTimer != psHWRInfo->ui64CRTimer) ||
		(psInfo->sFaultDevVAddr.uiAddr != psFaultDevVAddr->uiAddr))
	{
		MMU_FAULT_DATA *psFaultData = &psInfo->sMMUFaultData;

		psFaultData->eType = MMU_FAULT_TYPE_UNKNOWN;

		if (bPMFault)
		{
			/* PM fault and we dump PC details only */
			psFaultData->eTopLevel = MMU_LEVEL_0;
			psFaultData->eType     = MMU_FAULT_TYPE_PM;
			psFaultData->sLevelData[MMU_LEVEL_0].ui64Address = psPCDevPAddr->uiAddr;
		}
		else
		{
			RGXCheckFaultAddress(psDevInfo, psFaultDevVAddr, psPCDevPAddr, psFaultData);
		}

		_RecordFaultInfo(psDevInfo, psInfo,
					*psFaultDevVAddr, *psPCDevPAddr, psHWRInfo->ui64CRTimer,
					_PageSizeHWToBytes(ui32PageSize));

	}

	RGXDumpFaultAddressHostView(&psInfo->sMMUFaultData, pfnDumpDebugPrintf, pvDumpDebugFile, DD_NORMAL_INDENT);

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED)
	{
		_PrintFaultInfo(pfnDumpDebugPrintf, psDevInfo->psDeviceNode, pvDumpDebugFile, psInfo, DD_NORMAL_INDENT);
	}

	OSLockRelease(psDevInfo->hDebugFaultInfoLock);
}

void RGXConvertOSTimestampToSAndNS(IMG_UINT64 ui64OSTimer,
							IMG_UINT64 *pui64Seconds,
							IMG_UINT64 *pui64Nanoseconds)
{
	IMG_UINT32 ui32Remainder;

	*pui64Seconds = OSDivide64r64(ui64OSTimer, 1000000000, &ui32Remainder);
	*pui64Nanoseconds = ui64OSTimer - (*pui64Seconds * 1000000000ULL);
}


/*!
*******************************************************************************

 @Function	RGXDebugRequestProcess

 @Description

 This function will print out the debug for the specified level of verbosity

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input pvDumpDebugFile     - Optional file identifier to be passed to the
                              'printf' function if required
 @Input psDevInfo           - RGX device info
 @Input ui32VerbLevel       - Verbosity level

 @Return   void

******************************************************************************/
static
void RGXDebugRequestProcess(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 ui32VerbLevel)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	PVRSRV_DEV_POWER_STATE  ePowerState;
	IMG_BOOL                bRGXPoweredON;
	RGXFWIF_TRACEBUF        *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	const RGXFWIF_OSDATA    *psFwOsData = psDevInfo->psRGXFWIfFwOsData;
	IMG_BOOL                bPwrLockAlreadyHeld;

	bPwrLockAlreadyHeld = PVRSRVPwrLockIsLockedByMe(psDeviceNode);
	if (!bPwrLockAlreadyHeld)
	{
		/* Only acquire the power-lock if not already held by the calling context */
		eError = PVRSRVPowerLock(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to acquire lock (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
			return;
		}
	}
	/* This should satisfy all accesses below */
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfOsInit->sRGXCompChecks,
	                           INVALIDATE);
	eError = PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Error retrieving RGX power state. No debug info dumped.",
				__func__));
		goto Exit;
	}

	if (PVRSRV_VZ_MODE_IS(NATIVE) && (RGX_NUM_DRIVERS_SUPPORTED > 1))
	{
		PVR_DUMPDEBUG_LOG("Mismatch between the number of Operating Systems supported by KM driver (%d) and FW (%d)",
						   1, RGX_NUM_DRIVERS_SUPPORTED);
	}

	PVR_DUMPDEBUG_LOG("------[ RGX Device ID:%d Start ]------", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

	bRGXPoweredON = (ePowerState == PVRSRV_DEV_POWER_STATE_ON);

	PVR_DUMPDEBUG_LOG("------[ RGX Info ]------");
	PVR_DUMPDEBUG_LOG("Device Node (Info): %p (%p)", psDevInfo->psDeviceNode, psDevInfo);
	DevicememHistoryDumpRecordStats(psDevInfo->psDeviceNode, pfnDumpDebugPrintf, pvDumpDebugFile);
	PVR_DUMPDEBUG_LOG("RGX BVNC: %d.%d.%d.%d (%s)", psDevInfo->sDevFeatureCfg.ui32B,
											   psDevInfo->sDevFeatureCfg.ui32V,
											   psDevInfo->sDevFeatureCfg.ui32N,
											   psDevInfo->sDevFeatureCfg.ui32C,
											   PVR_ARCH_NAME);
	PVR_DUMPDEBUG_LOG("RGX Device State: %s", PVRSRVGetDebugDevStateString(psDeviceNode->eDevState));
	PVR_DUMPDEBUG_LOG("RGX Power State: %s", _RGXGetDebugDevPowerStateString(ePowerState));

	if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		if (psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated)
		{
			PVR_DUMP_FIRMWARE_INFO(psDevInfo->psRGXFWIfOsInit->sRGXCompChecks);
		}
		else
		{
			PVR_DUMPDEBUG_LOG("FW info: UNINITIALIZED");
		}
	}
	else
	{
		PVR_DUMP_FIRMWARE_INFO_HDR(psDevInfo->sFWInfoHeader);
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TILE_REGION_PROTECTION))
	{
#if defined(SUPPORT_TRP)
		PVR_DUMPDEBUG_LOG("TRP: HW support - Yes; SW enabled");
#else
		PVR_DUMPDEBUG_LOG("TRP: HW support - Yes; SW disabled");
#endif
	}
	else
	{
		PVR_DUMPDEBUG_LOG("TRP: HW support - No");
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, WORKGROUP_PROTECTION))
	{
#if defined(SUPPORT_WGP)
		PVR_DUMPDEBUG_LOG("WGP: HW support - Yes; SW enabled");
#else
		PVR_DUMPDEBUG_LOG("WGP: HW support - Yes; SW disabled");
#endif
	}
	else
	{
		PVR_DUMPDEBUG_LOG("WGP: HW support - No");
	}

	RGXDumpRGXDebugSummary(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, bRGXPoweredON);

	/* Dump out the kernel CCB. */
	{
		const RGXFWIF_CCB_CTL *psKCCBCtl = psDevInfo->psKernelCCBCtl;
		const RGXFWIF_CCB_CTL *psKCCBCtlLocal = psDevInfo->psKernelCCBCtlLocal;
		RGXFwSharedMemCacheOpPtr(psDevInfo->psKernelCCBCtl, INVALIDATE);

		if (psKCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Kernel CCB WO:0x%X RO:0x%X",
							  psKCCBCtlLocal->ui32WriteOffset,
							  psKCCBCtl->ui32ReadOffset);
		}
	}

	/* Dump out the firmware CCB. */
	{
		const RGXFWIF_CCB_CTL *psFCCBCtl = psDevInfo->psFirmwareCCBCtl;
		const RGXFWIF_CCB_CTL *psFCCBCtlLocal = psDevInfo->psFirmwareCCBCtlLocal;
		RGXFwSharedMemCacheOpPtr(psDevInfo->psFirmwareCCBCtl, INVALIDATE);

		if (psFCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Firmware CCB WO:0x%X RO:0x%X",
							   psFCCBCtl->ui32WriteOffset,
							   psFCCBCtlLocal->ui32ReadOffset);
		}
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* Dump out the Workload estimation CCB. */
		const RGXFWIF_CCB_CTL *psWorkEstCCBCtl = psDevInfo->psWorkEstFirmwareCCBCtl;
		const RGXFWIF_CCB_CTL *psWorkEstCCBCtlLocal = psDevInfo->psWorkEstFirmwareCCBCtlLocal;

		if (psWorkEstCCBCtl != NULL)
		{
			RGXFwSharedMemCacheOpPtr(psWorkEstCCBCtl, INVALIDATE);
			PVR_DUMPDEBUG_LOG("RGX WorkEst CCB WO:0x%X RO:0x%X",
							  psWorkEstCCBCtl->ui32WriteOffset,
							  psWorkEstCCBCtlLocal->ui32ReadOffset);
		}
	}
#endif

	RGXFwSharedMemCacheOpPtr(psFwOsData,
	                         INVALIDATE);

	if (psFwOsData != NULL)
	{
		/* Dump the KCCB commands executed */
		PVR_DUMPDEBUG_LOG("RGX Kernel CCB commands executed = %d",
						  psFwOsData->ui32KCCBCmdsExecuted);

#if defined(PVRSRV_STALLED_CCB_ACTION)
		/* Dump the number of times we have performed a forced UFO update,
		 * and (if non-zero) the timestamp of the most recent occurrence/
		 */
		PVR_DUMPDEBUG_LOG("RGX SLR: Forced UFO updates requested = %d",
						  psFwOsData->ui32ForcedUpdatesRequested);
		if (psFwOsData->ui32ForcedUpdatesRequested > 0)
		{
			IMG_UINT8 ui8Idx;
			IMG_UINT64 ui64Seconds, ui64Nanoseconds;

			if (psFwOsData->ui64LastForcedUpdateTime > 0ULL)
			{
				RGXConvertOSTimestampToSAndNS(psFwOsData->ui64LastForcedUpdateTime, &ui64Seconds, &ui64Nanoseconds);
				PVR_DUMPDEBUG_LOG("RGX SLR: (most recent forced update was around %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC ")",
								  ui64Seconds, ui64Nanoseconds);
			}
			else
			{
				PVR_DUMPDEBUG_LOG("RGX SLR: (unable to force update as fence contained no sync checkpoints)");
			}
			/* Dump SLR log */
			if (psFwOsData->sSLRLogFirst.aszCCBName[0])
			{
				RGXConvertOSTimestampToSAndNS(psFwOsData->sSLRLogFirst.ui64Timestamp, &ui64Seconds, &ui64Nanoseconds);
				PVR_DUMPDEBUG_LOG("RGX SLR:{%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC
								  "} Fence found on context 0x%x '%.*s' has %d UFOs",
								  ui64Seconds, ui64Nanoseconds,
								  psFwOsData->sSLRLogFirst.ui32FWCtxAddr,
								  PVR_SLR_LOG_STRLEN, psFwOsData->sSLRLogFirst.aszCCBName,
								  psFwOsData->sSLRLogFirst.ui32NumUFOs);
			}
			for (ui8Idx=0; ui8Idx<PVR_SLR_LOG_ENTRIES;ui8Idx++)
			{
				if (psFwOsData->sSLRLog[ui8Idx].aszCCBName[0])
				{
					RGXConvertOSTimestampToSAndNS(psFwOsData->sSLRLog[ui8Idx].ui64Timestamp, &ui64Seconds, &ui64Nanoseconds);
					PVR_DUMPDEBUG_LOG("RGX SLR:[%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC
									  "] Fence found on context 0x%x '%.*s' has %d UFOs",
									  ui64Seconds, ui64Nanoseconds,
									  psFwOsData->sSLRLog[ui8Idx].ui32FWCtxAddr,
									  PVR_SLR_LOG_STRLEN, psFwOsData->sSLRLog[ui8Idx].aszCCBName,
									  psFwOsData->sSLRLog[ui8Idx].ui32NumUFOs);
				}
			}
		}
#else
		PVR_DUMPDEBUG_LOG("RGX SLR: Disabled");
#endif

		/* Dump the error counts */
		PVR_DUMPDEBUG_LOG("RGX Errors: WGP:%d, TRP:%d",
						  psDevInfo->sErrorCounts.ui32WGPErrorCount,
						  psDevInfo->sErrorCounts.ui32TRPErrorCount);

		/* Dump the IRQ info for threads or OS IDs */
#if defined(RGX_FW_IRQ_OS_COUNTERS)
		/* only Host has access to registers containing IRQ counters */
		if (!PVRSRV_VZ_MODE_IS(GUEST))
#endif
		{
			IMG_UINT32 ui32idx;

			for_each_irq_cnt(ui32idx)
			{
				IMG_UINT32 ui32IrqCnt;

				get_irq_cnt_val(ui32IrqCnt, ui32idx, psDevInfo);
				if (ui32IrqCnt)
				{
					PVR_DUMPDEBUG_LOG(MSG_IRQ_CNT_TYPE "%u: FW IRQ count = %u", ui32idx, ui32IrqCnt);
#if defined(RGX_FW_IRQ_OS_COUNTERS)
					if (ui32idx == RGXFW_HOST_DRIVER_ID)
#endif
					{
						PVR_DUMPDEBUG_LOG("Last sampled IRQ count in LISR = %u", psDevInfo->aui32SampleIRQCount[ui32idx]);
					}
				}
			}
		}
	}

	/* Dump the FW Sys config flags on the Host */
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
		IMG_CHAR sFwSysFlagsDescription[MAX_FW_DESCRIPTION_LENGTH];

		if (!psFwSysData)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Fw Sys Data is not mapped into CPU space", __func__));
			goto Exit;
		}

		RGXFwSharedMemCacheOpValue(psFwSysData->ui32ConfigFlags,
		                           INVALIDATE);

		_GetFwSysFlagsDescription(sFwSysFlagsDescription, MAX_FW_DESCRIPTION_LENGTH, psFwSysData->ui32ConfigFlags);
		PVR_DUMPDEBUG_LOG("FW System config flags = 0x%08X (%s)", psFwSysData->ui32ConfigFlags, sFwSysFlagsDescription);
	}

	/* Dump the FW OS config flags */
	{
		IMG_CHAR sFwOsFlagsDescription[MAX_FW_DESCRIPTION_LENGTH];

		if (!psFwOsData)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Fw Os Data is not mapped into CPU space", __func__));
			goto Exit;
		}

		_GetFwOsFlagsDescription(sFwOsFlagsDescription, MAX_FW_DESCRIPTION_LENGTH, psFwOsData->ui32FwOsConfigFlags);
		PVR_DUMPDEBUG_LOG("FW OS config flags = 0x%08X (%s)", psFwOsData->ui32FwOsConfigFlags, sFwOsFlagsDescription);
	}

	if ((bRGXPoweredON) && !PVRSRV_VZ_MODE_IS(GUEST))
	{
		eError = RGXDumpRGXRegisters(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: RGXDumpRGXRegisters failed (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
		}
	}
	else
	{
		PVR_DUMPDEBUG_LOG(" (!) %s. No registers dumped", PVRSRV_VZ_MODE_IS(GUEST) ? "Guest Mode of operation" : "RGX power is down");
	}

	PVR_DUMPDEBUG_LOG("------[ RGX FW Trace Info ]------");

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MEDIUM))
	{
		IMG_INT tid;
		/* Dump FW trace information */
		if (psRGXFWIfTraceBufCtl != NULL)
		{
			RGX_FWT_LOGTYPE eFWTLogType = psDevInfo->eDebugDumpFWTLogType;

			if (eFWTLogType == RGX_FWT_LOGTYPE_NONE)
			{
				PVR_DUMPDEBUG_LOG("Firmware trace printing disabled.");
			}
			else
			{
				RGXFwSharedMemCacheOpValue(psRGXFWIfTraceBufCtl->ui32LogType, INVALIDATE);

				for (tid = 0; tid < RGXFW_THREAD_NUM; tid++)
				{
					IMG_UINT32 *pui32TraceBuffer;

					if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
					{
						PVR_DUMPDEBUG_LOG("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
										  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
										  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
										  );
					}
					else
					{
						PVR_DUMPDEBUG_LOG("Debug log type: none");
					}

					pui32TraceBuffer = psDevInfo->apui32TraceBuffer[tid];

					/* Skip if trace buffer is not allocated */
					if (pui32TraceBuffer == NULL)
					{
						PVR_DUMPDEBUG_LOG("RGX FW thread %d: Trace buffer not yet allocated",tid);
						continue;
					}

					RGXFwSharedMemCacheOpValue(psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer, INVALIDATE);
					PVR_DUMPDEBUG_LOG("------[ RGX FW thread %d trace START ]------", tid);
					PVR_DUMPDEBUG_LOG("FWT[traceptr]: %X", psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer);
					PVR_DUMPDEBUG_LOG("FWT[tracebufsize]: %X", psDevInfo->ui32TraceBufSizeInDWords);

					if (eFWTLogType == RGX_FWT_LOGTYPE_BINARY)
					{
						RGXDumpFirmwareTraceBinary(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, psRGXFWIfTraceBufCtl, tid);
					}
					else if (eFWTLogType == RGX_FWT_LOGTYPE_DECODED)
					{
						RGXDumpFirmwareTraceDecoded(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, psRGXFWIfTraceBufCtl, tid);
					}
					else if (eFWTLogType == RGX_FWT_LOGTYPE_PARTIAL)
					{
						RGXDumpFirmwareTracePartial(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, psRGXFWIfTraceBufCtl, tid);
					}

					PVR_DUMPDEBUG_LOG("------[ RGX FW thread %d trace END ]------", tid);
				}
			}
		}

		{
			if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_HIGH))
			{
				PVR_DUMPDEBUG_LOG("------[ Full CCB Status ]------");
			}
			else
			{
				PVR_DUMPDEBUG_LOG("------[ FWCtxs Next CMD ]------");
			}

			RGXDumpAllContextInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
		}
	}

	PVR_DUMPDEBUG_LOG("------[ RGX Device ID:%d End ]------", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

Exit:
	if (!bPwrLockAlreadyHeld)
	{
		PVRSRVPowerUnlock(psDeviceNode);
	}
}

/*!
 ******************************************************************************

 @Function	RGXDebugRequestNotify

 @Description Dump the debug data for RGX

 ******************************************************************************/
static void RGXDebugRequestNotify(PVRSRV_DBGREQ_HANDLE hDbgRequestHandle,
		IMG_UINT32 ui32VerbLevel,
		DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
		void *pvDumpDebugFile)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = hDbgRequestHandle;

	/* Only action the request if we've fully init'ed */
	if (psDevInfo->bDevInit2Done)
	{
		RGXDebugRequestProcess(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, ui32VerbLevel);
	}
}

PVRSRV_ERROR RGXDebugInit(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	return PVRSRVRegisterDeviceDbgRequestNotify(&psDevInfo->hDbgReqNotify,
		                                        psDevInfo->psDeviceNode,
		                                        RGXDebugRequestNotify,
		                                        DEBUG_REQUEST_RGX,
		                                        psDevInfo);
}

PVRSRV_ERROR RGXDebugDeinit(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->hDbgReqNotify)
	{
		return PVRSRVUnregisterDeviceDbgRequestNotify(psDevInfo->hDbgReqNotify);
	}

	/* No notifier registered */
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (rgxdebug_common.c)
******************************************************************************/
