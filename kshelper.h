/*++

Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:

    kshelper.h

Abstract:

    Kernel streaming helper functions

--*/
#ifndef __KSHELPER_H_
#define __KSHELPER_H_

#include <portcls.h>
#include <ksdebug.h>

PWAVEFORMATEX                   GetWaveFormatEx
(
    IN  PKSDATAFORMAT           pDataFormat
);

NTSTATUS                        PropertyHandler_BasicSupport
(
    IN  PPCPROPERTY_REQUEST     PropertyRequest,
    IN  ULONG                   Flags,
    IN  DWORD                   PropTypeSetId
);

NTSTATUS                        ValidatePropertyParams
(
    IN PPCPROPERTY_REQUEST      PropertyRequest, 
    IN ULONG                    cbValueSize,
    IN ULONG                    cbInstanceSize = 0 
);

#endif
