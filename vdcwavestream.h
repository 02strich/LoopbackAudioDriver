/*
Module Name:
    vdcwave.h

Abstract:
    Definition of wavecyclic miniport class.
*/

#ifndef __VDCWAVESTREAM_H_
#define __VDCWAVESTREAM_H_

#include "vdcwave.h"

///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveCyclicStream 
//   

class CMiniportWaveCyclicStream : public IMiniportWaveCyclicStream, public IDmaChannel, public CUnknown {
private:
  long zoh_process (PWORD source, PWORD destination, long input_frames, long output_frames, int channels);
  double CMiniportWaveCyclicStream::floor(double val);
protected:
  PCMiniportWaveCyclic        m_pMiniport;        // Miniport that created us  
  BOOLEAN                     m_fCapture;         // Capture or render.
  BOOLEAN                     m_fFormat16Bit;     // 16- or 8-bit samples.
  BOOLEAN                     m_fFormatStereo;    // Two or one channel.
  KSSTATE                     m_ksState;          // Stop, pause, run.
  ULONG                       m_ulPin;            // Pin Id.

  PRKDPC                      m_pDpc;             // Deferred procedure call object
  PKTIMER                     m_pTimer;           // Timer object

  BOOLEAN                     m_fDmaActive;       // Dma currently active? 
  ULONG                       m_ulDmaPosition;    // Position in Dma
  PVOID                       m_pvDmaBuffer;      // Dma buffer pointer
  ULONG                       m_ulDmaBufferSize;  // Size of dma buffer
  ULONG                       m_ulDmaMovementRate;// Rate of transfer specific to system
  ULONGLONG                   m_ullDmaTimeStamp;  // Dma time elasped 

  PVOID						  myBuffer;
  LONG						  myBufferSize;
  LONG						  myBufferLocked;
  LONG						  myBufferWritePos;
  LONG						  myBufferReadPos;
  LONG						  myBufferReading;

public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveCyclicStream);
    ~CMiniportWaveCyclicStream();

	IMP_IMiniportWaveCyclicStream;
    IMP_IDmaChannel;

    NTSTATUS Init
    ( 
        IN  PCMiniportWaveCyclic Miniport,
        IN  ULONG               Channel,
        IN  BOOLEAN             Capture,
        IN  PKSDATAFORMAT       DataFormat
    );

    // Friends
    friend class CMiniportWaveCyclic;
};
typedef CMiniportWaveCyclicStream *PCMiniportWaveCyclicStream;

#endif

