/*
Module Name:
  vdcwavestream.cpp

Abstract:
  WaveCyclicStream-Miniport and IDmaChannel implementation. Does nothing HW related.
*/

#include "vdcaudio.h"
#include "common.h"
#include "vdcwave.h"
#include "vdcwavestream.h"

#define DBGMESSAGE "[VDC-Audio] basedma.cpp: "
#define DBGPRINT(x) DbgPrint(DBGMESSAGE x)

// Globale Variablen:
PVOID myBuffer=NULL;
LONG myBufferSize=0;
LONG myBufferLocked=TRUE;
LONG myBufferWritePos=0;
LONG myBufferReadPos=0;

//=============================================================================
CMiniportWaveCyclicStream::~CMiniportWaveCyclicStream(void)
/*
Routine Description:
  Destructor for wavecyclicstream 

Arguments:

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  DPF_ENTER(("[CMiniportWaveCyclicStream::~CMiniportWaveCyclicStream]"));

  if (NULL != m_pMiniport) {
      if (m_fCapture)
          m_pMiniport->m_fCaptureAllocated = FALSE;
      else
          m_pMiniport->m_fRenderAllocated = FALSE;
  }
  if (m_pTimer) {
      KeCancelTimer(m_pTimer);
      ExFreePool(m_pTimer);
  }

  if (m_pDpc)
      ExFreePool( m_pDpc );

  // Free the DMA buffer
  FreeBuffer();

} // ~CMiniportWaveCyclicStream

//=============================================================================
NTSTATUS CMiniportWaveCyclicStream::Init( 
  IN PCMiniportWaveCyclic         Miniport_,
  IN ULONG                        Pin_,
  IN BOOLEAN                      Capture_,
  IN PKSDATAFORMAT                DataFormat_
)
/*
Routine Description:
  Initializes the stream object. Allocate a DMA buffer, timer and DPC

Arguments:
  Miniport_ -
  Pin_ -
  Capture_ -
  DataFormat -
  DmaChannel_ -

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  DPF_ENTER(("[CMiniportWaveCyclicStream::Init]"));
  ASSERT(Miniport_);
  ASSERT(DataFormat_);

  m_pMiniport = Miniport_;

  m_fCapture = FALSE;
  m_fFormat16Bit = FALSE;
  m_fFormatStereo = FALSE;
  m_ksState = KSSTATE_STOP;
  m_ulPin = (ULONG)-1;

  m_pDpc = NULL;
  m_pTimer = NULL;

  m_fDmaActive = FALSE;
  m_ulDmaPosition = 0;
  m_pvDmaBuffer = NULL;
  m_ulDmaBufferSize = 0;
  m_ulDmaMovementRate = 0;    
  m_ullDmaTimeStamp = 0;



  NTSTATUS ntStatus = STATUS_SUCCESS;
  PWAVEFORMATEX pWfx;

  pWfx = GetWaveFormatEx(DataFormat_);
  if (!pWfx) {
    DPF(D_TERSE, ("Invalid DataFormat param in NewStream"));
    ntStatus = STATUS_INVALID_PARAMETER;
  }

  if (NT_SUCCESS(ntStatus)) {
    m_ulPin         = Pin_;
    m_fCapture      = Capture_;
    m_fFormatStereo = (pWfx->nChannels == 2);
    m_fFormat16Bit  = (pWfx->wBitsPerSample == 16);
    m_ksState       = KSSTATE_STOP;
    m_ulDmaPosition = 0;
    m_fDmaActive    = FALSE;
    m_pDpc          = NULL;
    m_pTimer        = NULL;
    m_pvDmaBuffer   = NULL;

    // If this is not the capture stream, create the output file.
    if (!m_fCapture) {
      DPF(D_TERSE, ("SaveData %X", &m_SaveData));
      ntStatus = m_SaveData.SetDataFormat(DataFormat_);
      if (NT_SUCCESS(ntStatus)) {
            ntStatus = m_SaveData.Initialize();
      }
    }
  }

  // Allocate DMA buffer for this stream.
  if (NT_SUCCESS(ntStatus)) {
      ntStatus = AllocateBuffer(m_pMiniport->m_MaxDmaBufferSize, NULL);
  }

  // Set sample frequency. Note that m_SampleRateSync access should
  // be syncronized.
  if (NT_SUCCESS(ntStatus)) {
    ntStatus = KeWaitForSingleObject(
      &m_pMiniport->m_SampleRateSync,
      Executive,
      KernelMode,
      FALSE,
      NULL
    );
    if (NT_SUCCESS(ntStatus)) {
      m_pMiniport->m_SamplingFrequency = pWfx->nSamplesPerSec;
      KeReleaseMutex(&m_pMiniport->m_SampleRateSync, FALSE);
    } else {
      DPF(D_TERSE, ("[SamplingFrequency Sync failed: %08X]", ntStatus));
    }
  }

  if (NT_SUCCESS(ntStatus)) {
      ntStatus = SetFormat(DataFormat_);
  }

  if (NT_SUCCESS(ntStatus)) {
      m_pDpc = (PRKDPC) ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(KDPC),
        VDCAUDIO_POOLTAG
      );
      if (!m_pDpc) {
        DPF(D_TERSE, ("[Could not allocate memory for DPC]"));
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
      }
  }

  if (NT_SUCCESS(ntStatus)) {
    m_pTimer = (PKTIMER) ExAllocatePoolWithTag(
      NonPagedPool,
      sizeof(KTIMER),
      VDCAUDIO_POOLTAG
    );
    if (!m_pTimer) {
      DPF(D_TERSE, ("[Could not allocate memory for Timer]"));
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  if (NT_SUCCESS(ntStatus)) {
    KeInitializeDpc(m_pDpc, TimerNotify, m_pMiniport);
    KeInitializeTimerEx(m_pTimer, NotificationTimer);
  }

  return ntStatus;
} // Init

//=============================================================================
STDMETHODIMP_(NTSTATUS) CMiniportWaveCyclicStream::NonDelegatingQueryInterface( 
  IN  REFIID  Interface,
  OUT PVOID * Object 
)
/*
Routine Description:
  QueryInterface

Arguments:
  Interface - GUID
  Object - interface pointer to be returned

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  ASSERT(Object);

  if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
    *Object = PVOID(PUNKNOWN(PMINIPORTWAVECYCLICSTREAM(this)));
  } else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveCyclicStream)) {
    *Object = PVOID(PMINIPORTWAVECYCLICSTREAM(this));
  } else if (IsEqualGUIDAligned(Interface, IID_IDmaChannel)) {
    *Object = PVOID(PDMACHANNEL(this));
  } else {
    *Object = NULL;
  }

  if (*Object) {
    PUNKNOWN(*Object)->AddRef();
    return STATUS_SUCCESS;
  }

  return STATUS_INVALID_PARAMETER;
} // NonDelegatingQueryInterface
#pragma code_seg()

//=============================================================================
STDMETHODIMP CMiniportWaveCyclicStream::GetPosition(
  OUT PULONG                  Position
)
/*
Routine Description:
  The GetPosition function gets the current position of the DMA read or write
  pointer for the stream. Callers of GetPosition should run at
  IRQL <= DISPATCH_LEVEL.

Arguments:
  Position - Position of the DMA pointer

Return Value:
  NT status code.
*/
{
  if (m_fDmaActive) {
    ULONGLONG CurrentTime = KeQueryInterruptTime();

    ULONG TimeElapsedInMS = ( (ULONG) (CurrentTime - m_ullDmaTimeStamp) ) / 10000;

    ULONG ByteDisplacement = (m_ulDmaMovementRate * TimeElapsedInMS) / 1000;

    m_ulDmaPosition = (m_ulDmaPosition + ByteDisplacement) % m_ulDmaBufferSize;

    *Position = m_ulDmaPosition;

    m_ullDmaTimeStamp = CurrentTime;
  } else {
    *Position = m_ulDmaPosition;
  }

  return STATUS_SUCCESS;
} // GetPosition

//=============================================================================
STDMETHODIMP CMiniportWaveCyclicStream::NormalizePhysicalPosition(
  IN OUT PLONGLONG            PhysicalPosition
)
/*
Routine Description:
  Given a physical position based on the actual number of bytes transferred,
  NormalizePhysicalPosition converts the position to a time-based value of
  100 nanosecond units. Callers of NormalizePhysicalPosition can run at any IRQL.

Arguments:
  PhysicalPosition - On entry this variable contains the value to convert.
                     On return it contains the converted value

Return Value:
  NT status code.
*/
{
  *PhysicalPosition = ( _100NS_UNITS_PER_SECOND / ( 1 << ( m_fFormatStereo + m_fFormat16Bit ) ) * *PhysicalPosition ) / m_pMiniport->m_SamplingFrequency;
  return STATUS_SUCCESS;
} // NormalizePhysicalPosition

#pragma code_seg("PAGE")

//=============================================================================
STDMETHODIMP_(NTSTATUS) CMiniportWaveCyclicStream::SetFormat(
  IN  PKSDATAFORMAT           Format
)
/*
Routine Description:
  The SetFormat function changes the format associated with a stream.
  Callers of SetFormat should run at IRQL PASSIVE_LEVEL

Arguments:
  Format - Pointer to a KSDATAFORMAT structure which indicates the new format
           of the stream.

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  ASSERT(Format);
  DPF_ENTER(("[CMiniportWaveCyclicStream::SetFormat]"));

  NTSTATUS ntStatus = STATUS_INVALID_DEVICE_REQUEST;
  PWAVEFORMATEX pWfx;

  if (m_ksState != KSSTATE_RUN) {
    // MSVAD does not validate the format.
    pWfx = GetWaveFormatEx(Format);
    if (pWfx) {
      ntStatus = KeWaitForSingleObject(
        &m_pMiniport->m_SampleRateSync,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
      if (NT_SUCCESS(ntStatus)) {
          if (!m_fCapture) {
              ntStatus = m_SaveData.SetDataFormat(Format);
          }

          m_fFormatStereo = (pWfx->nChannels == 2);
          m_fFormat16Bit  = (pWfx->wBitsPerSample == 16);
          m_pMiniport->m_SamplingFrequency = pWfx->nSamplesPerSec;
          m_ulDmaMovementRate = pWfx->nAvgBytesPerSec;

          DPF(D_TERSE, ("New Format: %d", pWfx->nSamplesPerSec));
      }
      KeReleaseMutex(&m_pMiniport->m_SampleRateSync, FALSE);
    }
  }

  return ntStatus;
} // SetFormat

//=============================================================================
STDMETHODIMP_(ULONG) CMiniportWaveCyclicStream::SetNotificationFreq(
  IN  ULONG                   Interval,
  OUT PULONG                  FramingSize
)
/*
Routine Description:
  The SetNotificationFrequency function sets the frequency at which
  notification interrupts are generated. Callers of SetNotificationFrequency
  should run at IRQL PASSIVE_LEVEL.

Arguments:
  Interval - Value indicating the interval between interrupts,
             expressed in milliseconds
  FramingSize - Pointer to a ULONG value where the number of bytes equivalent
                to Interval milliseconds is returned

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  ASSERT(FramingSize);
  DPF_ENTER(("[CMiniportWaveCyclicStream::SetNotificationFreq]"));

  m_pMiniport->m_NotificationInterval = Interval;

  *FramingSize = ( 1 << ( m_fFormatStereo + m_fFormat16Bit ) ) * m_pMiniport->m_SamplingFrequency * Interval / 1000;

  return m_pMiniport->m_NotificationInterval;
} // SetNotificationFreq

//=============================================================================
STDMETHODIMP CMiniportWaveCyclicStream::SetState(
    IN  KSSTATE                 NewState
)
/*
Routine Description:
  The SetState function sets the new state of playback or recording for the
  stream. SetState should run at IRQL PASSIVE_LEVEL

Arguments:
  NewState - KSSTATE indicating the new state for the stream.

Return Value:
  NT status code.
*/
{
  PAGED_CODE();
  DPF_ENTER(("[CMiniportWaveCyclicStream::SetState]"));

  NTSTATUS ntStatus = STATUS_SUCCESS;

  // The acquire state is not distinguishable from the stop state for our purposes.
  if (NewState == KSSTATE_ACQUIRE) {
    NewState = KSSTATE_STOP;
  }

  if (m_ksState != NewState) {
    switch(NewState) {
      case KSSTATE_PAUSE:
        DPF(D_TERSE, ("KSSTATE_PAUSE"));
        m_fDmaActive = FALSE;
        break;

      case KSSTATE_RUN:
        DPF(D_TERSE, ("KSSTATE_RUN"));

		LARGE_INTEGER   delay;

        // Set the timer for DPC.
        m_ullDmaTimeStamp   = KeQueryInterruptTime();
        m_fDmaActive        = TRUE;
        delay.HighPart      = 0;
        delay.LowPart       = m_pMiniport->m_NotificationInterval;

        KeSetTimerEx(m_pTimer, delay, m_pMiniport->m_NotificationInterval, m_pDpc);
        break;

      case KSSTATE_STOP:
        DPF(D_TERSE, ("KSSTATE_STOP"));

        m_fDmaActive = FALSE;
        m_ulDmaPosition = 0;

        KeCancelTimer( m_pTimer );

        // Wait until all work items are completed.
        if (!m_fCapture) {
            m_SaveData.WaitAllWorkItems();
        }
        break;
    }

    m_ksState = NewState;
  }

  return ntStatus;
} // SetState

#pragma code_seg()

//=============================================================================
STDMETHODIMP_(void) CMiniportWaveCyclicStream::Silence(
  IN PVOID                    Buffer,
  IN ULONG                    ByteCount
)
/*
Routine Description:
  The Silence function is used to copy silence samplings to a certain location.
  Callers of Silence can run at any IRQL

Arguments:
  Buffer - Pointer to the buffer where the silence samplings should
           be deposited.
  ByteCount - Size of buffer indicating number of bytes to be deposited.

Return Value:
  NT status code.
*/
{
  RtlFillMemory(Buffer, ByteCount, m_fFormat16Bit ? 0 : 0x80);
} // Silence

#pragma code_seg("PAGE")
//=============================================================================
STDMETHODIMP_(NTSTATUS) CMiniportWaveCyclicStream::AllocateBuffer( 
  IN ULONG                    BufferSize,
  IN PPHYSICAL_ADDRESS        PhysicalAddressConstraint OPTIONAL 
)
/*
Routine Description:
  The AllocateBuffer function allocates a buffer associated with the DMA object. 
  The buffer is nonPaged.
  Callers of AllocateBuffer should run at a passive IRQL.

Arguments:
  BufferSize - Size in bytes of the buffer to be allocated. 
  PhysicalAddressConstraint - Optional constraint to place on the physical 
                              address of the buffer. If supplied, only the bits 
                              that are set in the constraint address may vary 
                              from the beginning to the end of the buffer. 
                              For example, if the desired buffer should not 
                              cross a 64k boundary, the physical address 
                              constraint 0x000000000000ffff should be specified

Return Value:
  NT status code.
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::AllocateBuffer]");

  // Adjust this cap as needed...
  ASSERT (BufferSize <= DMA_BUFFER_SIZE);

  NTSTATUS ntStatus = STATUS_SUCCESS;

  m_pvDmaBuffer = (PVOID) ExAllocatePoolWithTag(NonPagedPool, BufferSize, VDCAUDIO_POOLTAG);
  if (!m_pvDmaBuffer) {
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
  } else {
      m_ulDmaBufferSize = BufferSize;
  }

  return ntStatus;
} // AllocateBuffer
#pragma code_seg()

//=============================================================================
STDMETHODIMP_(ULONG) CMiniportWaveCyclicStream::AllocatedBufferSize(void)
/*
Routine Description:
  AllocatedBufferSize returns the size of the allocated buffer. 
  Callers of AllocatedBufferSize can run at any IRQL.

Arguments:

Return Value:
  ULONG
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::AllocatedBufferSize]");
  return m_ulDmaBufferSize;
} // AllocatedBufferSize

//=============================================================================
STDMETHODIMP_(ULONG) CMiniportWaveCyclicStream::BufferSize(void)
/*
Routine Description:
  BufferSize returns the size set by SetBufferSize or the allocated buffer size 
  if the buffer size has not been set. The DMA object does not actually use 
  this value internally. This value is maintained by the object to allow its 
  various clients to communicate the intended size of the buffer. This call 
  is often used to obtain the map size parameter to the Start member 
  function. Callers of BufferSize can run at any IRQL

Arguments:

Return Value:
  ULONG
*/
{
  return m_ulDmaBufferSize;
} // BufferSize

//=============================================================================
STDMETHODIMP_(void) CMiniportWaveCyclicStream::CopyFrom( 
    IN  PVOID                   Destination,
    IN  PVOID                   Source,
    IN  ULONG                   ByteCount 
)
/*
Routine Description:
  The CopyFrom function copies sample data from the DMA buffer. 
  Callers of CopyFrom can run at any IRQL

Arguments:
  Destination - Points to the destination buffer. 
  Source - Points to the source buffer. 
  ByteCount - Points to the source buffer. 

Return Value:
  void
*/
{
  ULONG i=0;
  ULONG FrameCount = ByteCount/2; //we guess 16-Bit sample rate
  if (!myBufferLocked) {
    InterlockedExchange(&myBufferLocked, TRUE);
    i=0;
    while ((myBufferWritePos != myBufferReadPos+1) && !((myBufferWritePos==0) && (myBufferReadPos==myBufferSize))) {
      ((PWORD)Destination)[i]=((PWORD)myBuffer)[myBufferReadPos];
      i++;
      myBufferReadPos++;
      if (myBufferReadPos >= myBufferSize) //Loop the buffer
	    myBufferReadPos=0;
      if (i >= FrameCount) //we have more data in the buffer than the caller would like to get
        break;
    }

	//just for debugging
    if (i<FrameCount)  {
      DbgPrint(DBGMESSAGE "CopyFrom - need more data! NeedCount=%d", FrameCount-i);
    }
    for (; i < FrameCount ; i++) {
      //if the caller want to read more data than the buffer size is,
      //we fill the rest with silence
      ((PWORD)Destination)[i]=0;
    }
    DbgPrint(DBGMESSAGE "CopyFrom TRUE ByteCount=%d", ByteCount);
    InterlockedExchange(&myBufferLocked, FALSE);
  } else {
    //in this case we can't obtain the data from buffer because it is locked
    //the best we can do (to satisfy the caller) is to fill the whole buffer with silence
    for (i=0; i < FrameCount ; i++) {
      ((PWORD)Destination)[i]=0;
    }
    DBGPRINT("CopyFrom FALSE");
  }
} // CopyFrom

//=============================================================================
STDMETHODIMP_(void) CMiniportWaveCyclicStream::CopyTo( 
  IN  PVOID                   Destination,
  IN  PVOID                   Source,
  IN  ULONG                   ByteCount
)
/*
Routine Description:
  The CopyTo function copies sample data to the DMA buffer. 
  Callers of CopyTo can run at any IRQL. 

Arguments:
  Destination - Points to the destination buffer. 
  Source - Points to the source buffer
  ByteCount - Number of bytes to be copied

Return Value:
  void
*/

{
  ULONG i=0;
  ULONG FrameCount = ByteCount/2; //we guess 16-Bit sample rate
  if (myBuffer==NULL) {
    ULONG bufSize=64*1024; //size in bytes
    DBGPRINT("Try to allocate buffer");
    myBuffer = (PVOID) ExAllocatePoolWithTag(NonPagedPool, bufSize, VDCAUDIO_POOLTAG);
    if (!myBuffer) {
      DBGPRINT("FAILED to allocate buffer");
    } else {
      DBGPRINT("Successfully allocated buffer");
      myBufferSize = bufSize/2; //myBufferSize in frames
      InterlockedExchange(&myBufferLocked, FALSE);
	}
  }
  
  if (!myBufferLocked) {
    DbgPrint(DBGMESSAGE "Fill Buffer ByteCount=%d", ByteCount);
    InterlockedExchange(&myBufferLocked, TRUE);

    i=0;
    while (i < FrameCount) {//while data is available
      //test wether we arrived at the read-pos (this should not happen)
      if (! ((myBufferWritePos+1 != myBufferReadPos) && !((myBufferReadPos==0) && (myBufferWritePos==myBufferSize)))) {
        DbgPrint(DBGMESSAGE "CopyTo - there is no space for new data! NeedCount=%d", FrameCount-i);
        break; //we have to break - because there is no space for the rest data
      }

      ((PWORD)myBuffer)[myBufferWritePos]=((PWORD)Source)[i];
      i++;
      myBufferWritePos++;
      if (myBufferWritePos >= myBufferSize) //Loop the buffer
	    myBufferWritePos=0;
    }
  InterlockedExchange(&myBufferLocked, FALSE);
  }
  m_SaveData.WriteData((PBYTE) Source, ByteCount);
} // CopyTo

//=============================================================================
#pragma code_seg("PAGE")
STDMETHODIMP_(void) CMiniportWaveCyclicStream::FreeBuffer(void)
/*
Routine Description:
  The FreeBuffer function frees the buffer allocated by AllocateBuffer. Because 
  the buffer is automatically freed when the DMA object is deleted, this 
  function is not normally used. Callers of FreeBuffer should run at 
  IRQL PASSIVE_LEVEL.

Arguments:

Return Value:
  void
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::FreeBuffer]");

  if ( m_pvDmaBuffer ) {
    ExFreePool( m_pvDmaBuffer );
    m_ulDmaBufferSize = 0;
    m_pvDmaBuffer = NULL;
  }
  if ( myBuffer ) {
    InterlockedExchange(&myBufferLocked, TRUE); //first lock the buffer, so nobody would try to read from myBuffer
    ExFreePool ( myBuffer );
    myBufferSize = 0;
    myBuffer = NULL;
  }
} // FreeBuffer
#pragma code_seg()

//=============================================================================
STDMETHODIMP_(PADAPTER_OBJECT) CMiniportWaveCyclicStream::GetAdapterObject(void)
/*
Routine Description:
  The GetAdapterObject function returns the DMA object's internal adapter 
  object. Callers of GetAdapterObject can run at any IRQL.

Arguments:

Return Value:
  PADAPTER_OBJECT - The return value is the object's internal adapter object.
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::GetAdapterObject]");

  // MSVAD does not have need a physical DMA channel. Therefore it 
  // does not have physical DMA structure.
    
  return NULL;
} // GetAdapterObject

//=============================================================================
STDMETHODIMP_(ULONG) CMiniportWaveCyclicStream::MaximumBufferSize(void)
/*
Routine Description:

Arguments:

Return Value:
  NT status code.
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::MaximumBufferSize]");
  return m_pMiniport->m_MaxDmaBufferSize;
} // MaximumBufferSize

//=============================================================================
STDMETHODIMP_(PHYSICAL_ADDRESS) CMiniportWaveCyclicStream::PhysicalAddress(void)
/*
Routine Description:
  MaximumBufferSize returns the size in bytes of the largest buffer this DMA 
  object is configured to support. Callers of MaximumBufferSize can run 
  at any IRQL

Arguments:

Return Value:
  PHYSICAL_ADDRESS - The return value is the size in bytes of the largest 
                     buffer this DMA object is configured to support.
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::PhysicalAddress]");

  PHYSICAL_ADDRESS pAddress;
  pAddress.QuadPart = (LONGLONG) m_pvDmaBuffer;
  return pAddress;
} // PhysicalAddress

//=============================================================================
STDMETHODIMP_(void) CMiniportWaveCyclicStream::SetBufferSize( 
  IN ULONG                    BufferSize 
)
/*
Routine Description:
  The SetBufferSize function sets the current buffer size. This value is set to 
  the allocated buffer size when AllocateBuffer is called. The DMA object does 
  not actually use this value internally. This value is maintained by the object 
  to allow its various clients to communicate the intended size of the buffer. 
  Callers of SetBufferSize can run at any IRQL.

Arguments:
  BufferSize - Current size in bytes.

Return Value:
  void
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::SetBufferSize]");

  if ( BufferSize <= m_ulDmaBufferSize ) {
    m_ulDmaBufferSize = BufferSize;
  } else {
    DPF(D_ERROR, ("Tried to enlarge dma buffer size"));
  }
} // SetBufferSize

//=============================================================================
STDMETHODIMP_(PVOID) CMiniportWaveCyclicStream::SystemAddress(void)
/*
Routine Description:
  The SystemAddress function returns the virtual system address of the 
  allocated buffer. Callers of SystemAddress can run at any IRQL.

Arguments:

Return Value:
  PVOID - The return value is the virtual system address of the 
          allocated buffer.
*/
{
  return m_pvDmaBuffer;
} // SystemAddress

//=============================================================================
STDMETHODIMP_(ULONG) CMiniportWaveCyclicStream::TransferCount(void)
/*
Routine Description:
  The TransferCount function returns the size in bytes of the buffer currently 
  being transferred by a slave DMA object. Callers of TransferCount can run 
  at any IRQL.

Arguments:

Return Value:
  ULONG - The return value is the size in bytes of the buffer currently 
          being transferred.
*/
{
  DBGPRINT("[CMiniportWaveCyclicStream::TransferCount]");
  return m_ulDmaBuff