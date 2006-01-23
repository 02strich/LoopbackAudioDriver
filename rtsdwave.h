/*
Module Name:
    rtsdwave.h

Abstract:
    Definition of wavecyclic miniport class.
*/

#ifndef __RTSDWAVE_H_
#define __RTSDWAVE_H_

#include "rtsdwave.h"

//=============================================================================
// Referenced Forward
//=============================================================================
void TimerNotify( 
    IN  PKDPC                   Dpc,
    IN  PVOID                   DeferredContext,
    IN  PVOID                   SA1, 
    IN  PVOID                   SA2 
);

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveCyclic 
//   

class CMiniportWaveCyclic : public IMiniportWaveCyclic, public CUnknown {
private:
  BOOL                        m_fCaptureAllocated;
  BOOL                        m_fRenderAllocated;

protected:
  PADAPTERCOMMON              m_AdapterCommon;    // Adapter common object
  PPORTWAVECYCLIC             m_Port;             // Callback interface
  PPCFILTER_DESCRIPTOR        m_FilterDescriptor; // Filter descriptor

  ULONG                       m_NotificationInterval; // milliseconds.
  ULONG                       m_SamplingFrequency;    // Frames per second.

  PSERVICEGROUP               m_ServiceGroup;     // For notification.
  KMUTEX                      m_SampleRateSync;   // Sync for sample rate 

  ULONG                       m_MaxDmaBufferSize; // Dma buffer size.

  // All the below members should be updated by the child classes
  ULONG                       m_MaxOutputStreams; // Max stream caps
  ULONG                       m_MaxInputStreams;
  ULONG                       m_MaxTotalStreams;

  ULONG                       m_MinChannels;      // Format caps
  ULONG                       m_MaxChannelsPcm;
  ULONG                       m_MinBitsPerSamplePcm;
  ULONG                       m_MaxBitsPerSamplePcm;
  ULONG                       m_MinSampleRatePcm;
  ULONG                       m_MaxSampleRatePcm;

protected:
  NTSTATUS ValidateFormat(IN PKSDATAFORMAT pDataFormat);
  NTSTATUS ValidatePcm(IN PWAVEFORMATEX pWfx);

public:
  DECLARE_STD_UNKNOWN();
  DEFINE_STD_CONSTRUCTOR(CMiniportWaveCyclic);
  ~CMiniportWaveCyclic();

  IMP_IMiniportWaveCyclic;

  //--> muss hier her, da CopyTo und CopyFrom in verschiedenen Stream-Instanzen aufgerufen werden.
  PVOID myBuffer;
  LONG myBufferSize;
  LONG myBufferLocked;
  LONG myBufferWritePos;
  LONG myBufferReadPos;
  LONG myBufferReading; //Determines wether there is a client that still reads data
  

  // Property Handler
  NTSTATUS PropertyHandlerGeneric(IN PPCPROPERTY_REQUEST PropertyRequest);
  NTSTATUS PropertyHandlerComponentId(IN PPCPROPERTY_REQUEST PropertyRequest);
  NTSTATUS PropertyHandlerCpuResources(IN PPCPROPERTY_REQUEST PropertyRequest);

  // Friends
  friend class                CMiniportWaveCyclicStream;
  friend class                CMiniportTopologySimple;
  friend void                 TimerNotify( 
      IN  PKDPC               Dpc, 
      IN  PVOID               DeferredContext, 
      IN  PVOID               SA1, 
      IN  PVOID               SA2 
  );
};
typedef CMiniportWaveCyclic *PCMiniportWaveCyclic;
#endif

