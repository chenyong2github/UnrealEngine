/* Header file automatically generated from Microsoft.Holographic.AppRemoting.idl */
/*
 * File built with Microsoft(R) MIDLRT Compiler Engine Version 10.00.0226 
 */

#pragma warning( disable: 4049 )  /* more than 64k source lines */

/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

/* verify that the <rpcsal.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCSAL_H_VERSION__
#define __REQUIRED_RPCSAL_H_VERSION__ 100
#endif

#include <rpc.h>
#include <rpcndr.h>

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */

#ifndef COM_NO_WINDOWS_H
#include <windows.h>
#include <ole2.h>
#endif /*COM_NO_WINDOWS_H*/
#ifndef __Microsoft2EHolographic2EAppRemoting_h__
#define __Microsoft2EHolographic2EAppRemoting_h__
#ifndef __Microsoft2EHolographic2EAppRemoting_p_h__
#define __Microsoft2EHolographic2EAppRemoting_p_h__


#pragma once

// Ensure that the setting of the /ns_prefix command line switch is consistent for all headers.
// If you get an error from the compiler indicating "warning C4005: 'CHECK_NS_PREFIX_STATE': macro redefinition", this
// indicates that you have included two different headers with different settings for the /ns_prefix MIDL command line switch
#if !defined(DISABLE_NS_PREFIX_CHECKS)
#define CHECK_NS_PREFIX_STATE "never"
#endif // !defined(DISABLE_NS_PREFIX_CHECKS)


#pragma push_macro("MIDL_CONST_ID")
#undef MIDL_CONST_ID
#define MIDL_CONST_ID const __declspec(selectany)


// Header files for imported files
#include "winrtbase.h"
#include "C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.18362.0\Windows.h"
// Importing Collections header
#include <windows.foundation.collections.h>

#if defined(__cplusplus) && !defined(CINTERFACE)
/* Forward Declarations */
#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IDataChannelReceivedHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler Microsoft::Holographic::AppRemoting::IDataChannelReceivedHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IDataChannelCloseHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler Microsoft::Holographic::AppRemoting::IDataChannelCloseHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IDataChannel;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel Microsoft::Holographic::AppRemoting::IDataChannel

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificate;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate Microsoft::Holographic::AppRemoting::ICertificate

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificateProviderCallback;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback Microsoft::Holographic::AppRemoting::ICertificateProviderCallback

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificateProvider;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider Microsoft::Holographic::AppRemoting::ICertificateProvider

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificateChain;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain Microsoft::Holographic::AppRemoting::ICertificateChain

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificateValidationCallback;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback Microsoft::Holographic::AppRemoting::ICertificateValidationCallback

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface ICertificateValidator;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator Microsoft::Holographic::AppRemoting::ICertificateValidator

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IAuthenticationProviderCallback;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback Microsoft::Holographic::AppRemoting::IAuthenticationProviderCallback

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IAuthenticationProvider;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider Microsoft::Holographic::AppRemoting::IAuthenticationProvider

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IAuthenticationReceiverCallback;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback Microsoft::Holographic::AppRemoting::IAuthenticationReceiverCallback

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IAuthenticationReceiver;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver Microsoft::Holographic::AppRemoting::IAuthenticationReceiver

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnRecognizedSpeechHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler Microsoft::Holographic::AppRemoting::IOnRecognizedSpeechHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IRemoteSpeech;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech Microsoft::Holographic::AppRemoting::IRemoteSpeech

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnConnectedHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler Microsoft::Holographic::AppRemoting::IOnConnectedHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnDisconnectedHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler Microsoft::Holographic::AppRemoting::IOnDisconnectedHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnSendFrameHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler Microsoft::Holographic::AppRemoting::IOnSendFrameHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnDataChannelCreatedHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler Microsoft::Holographic::AppRemoting::IOnDataChannelCreatedHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IOnListeningHandler;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler Microsoft::Holographic::AppRemoting::IOnListeningHandler

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IRemoteContext;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext Microsoft::Holographic::AppRemoting::IRemoteContext

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IRemoteContextStatics;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics Microsoft::Holographic::AppRemoting::IRemoteContextStatics

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IPlayerContext2;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 Microsoft::Holographic::AppRemoting::IPlayerContext2

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IPlayerContext;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext Microsoft::Holographic::AppRemoting::IPlayerContext

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            interface IPlayerContextStatics;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics Microsoft::Holographic::AppRemoting::IPlayerContextStatics

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__

// Parameterized interface forward declarations (C++)

// Collection interface definitions

#ifndef DEF___FIReference_1_boolean_USE
#define DEF___FIReference_1_boolean_USE
#if !defined(RO_NO_TEMPLATE_NAME)
namespace Windows { namespace Foundation {
template <>
struct __declspec(uuid("3c00fd60-2950-5939-a21a-2d12c5a01b8a"))
IReference<bool> : IReference_impl<Windows::Foundation::Internal::AggregateType<bool, boolean>> 
{
    static const wchar_t* z_get_rc_name_impl() 
    {
        return L"Windows.Foundation.IReference`1<Boolean>"; 
    }
};
// Define a typedef for the parameterized interface specialization's mangled name.
// This allows code which uses the mangled name for the parameterized interface to access the
// correct parameterized interface specialization.
typedef IReference<bool> __FIReference_1_boolean_t;
#define __FIReference_1_boolean Windows::Foundation::__FIReference_1_boolean_t
/* Foundation */ } /* Windows */ } 

////  Define an alias for the C version of the interface for compatibility purposes.
//#define __FIReference_1_boolean Windows::Foundation::IReference<boolean>
//#define __FIReference_1_boolean_t Windows::Foundation::IReference<boolean>
#endif // !defined(RO_NO_TEMPLATE_NAME)
#endif /* DEF___FIReference_1_boolean_USE */




namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            struct CertificateValidationResult;
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


#ifndef DEF___FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_USE
#define DEF___FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_USE
#if !defined(RO_NO_TEMPLATE_NAME)
namespace Windows { namespace Foundation {
template <>
struct __declspec(uuid("86860a06-1041-5586-b641-3d1b3eb54326"))
IReference<struct Microsoft::Holographic::AppRemoting::CertificateValidationResult> : IReference_impl<struct Microsoft::Holographic::AppRemoting::CertificateValidationResult> 
{
    static const wchar_t* z_get_rc_name_impl() 
    {
        return L"Windows.Foundation.IReference`1<Microsoft.Holographic.AppRemoting.CertificateValidationResult>"; 
    }
};
// Define a typedef for the parameterized interface specialization's mangled name.
// This allows code which uses the mangled name for the parameterized interface to access the
// correct parameterized interface specialization.
typedef IReference<struct Microsoft::Holographic::AppRemoting::CertificateValidationResult> __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_t;
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult Windows::Foundation::__FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_t
/* Foundation */ } /* Windows */ } 

////  Define an alias for the C version of the interface for compatibility purposes.
//#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult Windows::Foundation::IReference<Microsoft::Holographic::AppRemoting::CertificateValidationResult>
//#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_t Windows::Foundation::IReference<Microsoft::Holographic::AppRemoting::CertificateValidationResult>
#endif // !defined(RO_NO_TEMPLATE_NAME)
#endif /* DEF___FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_USE */






/*
 *
 * Struct Microsoft.Holographic.AppRemoting.ConnectionState
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version, v1_enum] */
            enum ConnectionState : int
            {
                ConnectionState_Disconnected,
                ConnectionState_Connecting,
                ConnectionState_Connected,
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.ConnectionFailureReason
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version, v1_enum] */
            enum ConnectionFailureReason : int
            {
                ConnectionFailureReason_None,
                ConnectionFailureReason_Unknown,
                ConnectionFailureReason_NoServerCertificate,
                ConnectionFailureReason_HandshakePortBusy,
                ConnectionFailureReason_HandshakeUnreachable,
                ConnectionFailureReason_HandshakeConnectionFailed,
                ConnectionFailureReason_AuthenticationFailed,
                ConnectionFailureReason_RemotingVersionMismatch,
                ConnectionFailureReason_IncompatibleTransportProtocols,
                ConnectionFailureReason_HandshakeFailed,
                ConnectionFailureReason_TransportPortBusy,
                ConnectionFailureReason_TransportUnreachable,
                ConnectionFailureReason_TransportConnectionFailed,
                ConnectionFailureReason_ProtocolVersionMismatch,
                ConnectionFailureReason_ProtocolError,
                ConnectionFailureReason_VideoCodecNotAvailable,
                ConnectionFailureReason_Canceled,
                ConnectionFailureReason_ConnectionLost,
                ConnectionFailureReason_DeviceLost,
                ConnectionFailureReason_DisconnectRequest,
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.PreferredVideoCodec
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version, v1_enum] */
            enum PreferredVideoCodec : int
            {
                PreferredVideoCodec_Default,
                PreferredVideoCodec_H264,
                PreferredVideoCodec_H265,
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.DataChannelPriority
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version, v1_enum] */
            enum DataChannelPriority : int
            {
                DataChannelPriority_Low,
                DataChannelPriority_Medium,
                DataChannelPriority_High,
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.DataChannelReceivedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("c972e7dd-da67-4815-b3d4-9828df3a045c")] */
            MIDL_INTERFACE("c972e7dd-da67-4815-b3d4-9828df3a045c")
            IDataChannelReceivedHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */unsigned int dataLength,
                    /* [size_is(dataLength), in] */::byte * data
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IDataChannelReceivedHandler=_uuidof(IDataChannelReceivedHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.DataChannelCloseHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("77467e89-a9fd-4d8d-aea2-bfbba50a32a7")] */
            MIDL_INTERFACE("77467e89-a9fd-4d8d-aea2-bfbba50a32a7")
            IDataChannelCloseHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IDataChannelCloseHandler=_uuidof(IDataChannelCloseHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IDataChannel
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IDataChannel[] = L"Microsoft.Holographic.AppRemoting.IDataChannel";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("66b1e9f7-5ece-47f5-b783-91dacaf06188")] */
            MIDL_INTERFACE("66b1e9f7-5ece-47f5-b783-91dacaf06188")
            IDataChannel : public IInspectable
            {
            public:
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnDataReceived(
                    /* [in] */Microsoft::Holographic::AppRemoting::IDataChannelReceivedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnDataReceived(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE SendData(
                    /* [in] */unsigned int dataLength,
                    /* [size_is(dataLength), in] */::byte * data,
                    /* [in] */::boolean guaranteedDelivery
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Close(void) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnClosed(
                    /* [in] */Microsoft::Holographic::AppRemoting::IDataChannelCloseHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnClosed(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IDataChannel=_uuidof(IDataChannel);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannel;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificate
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificate[] = L"Microsoft.Holographic.AppRemoting.ICertificate";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("a0669db2-2157-4227-8800-0a357c2a2900")] */
            MIDL_INTERFACE("a0669db2-2157-4227-8800-0a357c2a2900")
            ICertificate : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE GetCertificatePfx(
                    /* [out] */unsigned int * resultLength,
                    /* [size_is(, *(resultLength)), retval, out] */::byte * * result
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE GetSubjectName(
                    /* [out, retval] */HSTRING * result
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE GetPfxPassword(
                    /* [out, retval] */HSTRING * result
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificate=_uuidof(ICertificate);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificate;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateProviderCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateProviderCallback[] = L"Microsoft.Holographic.AppRemoting.ICertificateProviderCallback";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("6797f815-e529-4697-b61d-68ef5082ad19")] */
            MIDL_INTERFACE("6797f815-e529-4697-b61d-68ef5082ad19")
            ICertificateProviderCallback : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE CertificateReceived(
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificate * certificate
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Cancel(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificateProviderCallback=_uuidof(ICertificateProviderCallback);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateProvider
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateProvider[] = L"Microsoft.Holographic.AppRemoting.ICertificateProvider";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("eaf80ccd-2d8a-4f43-a64f-ba1f91bdc25f")] */
            MIDL_INTERFACE("eaf80ccd-2d8a-4f43-a64f-ba1f91bdc25f")
            ICertificateProvider : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE GetCertificate(
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateProviderCallback * callback
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificateProvider=_uuidof(ICertificateProvider);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateChain
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateChain[] = L"Microsoft.Holographic.AppRemoting.ICertificateChain";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("4415b01f-ce8f-4599-a7c8-0b5e6a277708")] */
            MIDL_INTERFACE("4415b01f-ce8f-4599-a7c8-0b5e6a277708")
            ICertificateChain : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE GetCertificate(
                    /* [in] */unsigned int index,
                    /* [out] */unsigned int * resultLength,
                    /* [size_is(, *(resultLength)), retval, out] */::byte * * result
                    ) = 0;
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_Length(
                    /* [out, retval] */unsigned int * value
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificateChain=_uuidof(ICertificateChain);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateChain;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__) */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.CertificateValidationResult
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version] */
            struct CertificateValidationResult
            {
                ::boolean TrustedRoot;
                ::boolean Revoked;
                ::boolean Expired;
                ::boolean WrongUsage;
                __FIReference_1_boolean * NameMismatch;
                ::boolean RevocationCheckFailed;
                ::boolean InvalidCertOrChain;
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateValidationCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateValidationCallback[] = L"Microsoft.Holographic.AppRemoting.ICertificateValidationCallback";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("c8945a4c-33db-4143-8c9e-af35a2e57809")] */
            MIDL_INTERFACE("c8945a4c-33db-4143-8c9e-af35a2e57809")
            ICertificateValidationCallback : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE CertificateValidated(
                    /* [in] */struct Microsoft::Holographic::AppRemoting::CertificateValidationResult result
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Cancel(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificateValidationCallback=_uuidof(ICertificateValidationCallback);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateValidator
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateValidator[] = L"Microsoft.Holographic.AppRemoting.ICertificateValidator";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("ac9bd062-c81e-479f-b33c-ac979c6712e5")] */
            MIDL_INTERFACE("ac9bd062-c81e-479f-b33c-ac979c6712e5")
            ICertificateValidator : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE ValidateCertificate(
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateChain * certificateChain,
                    /* [in] */HSTRING serverName,
                    /* [in] */::boolean forceRevocationCheck,
                    /* [in] */__FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * systemValidationResult,
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateValidationCallback * callback
                    ) = 0;
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_PerformSystemValidation(
                    /* [out, retval] */::boolean * value
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_ICertificateValidator=_uuidof(ICertificateValidator);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationProviderCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationProviderCallback[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationProviderCallback";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("c4bc488f-8cd4-4118-ac15-7859f3916676")] */
            MIDL_INTERFACE("c4bc488f-8cd4-4118-ac15-7859f3916676")
            IAuthenticationProviderCallback : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE TokenReceived(
                    /* [in] */HSTRING token
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Cancel(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IAuthenticationProviderCallback=_uuidof(IAuthenticationProviderCallback);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationProvider
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationProvider[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationProvider";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("af3d4cd6-8484-445a-b8b3-b96da5711e3e")] */
            MIDL_INTERFACE("af3d4cd6-8484-445a-b8b3-b96da5711e3e")
            IAuthenticationProvider : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE GetToken(
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationProviderCallback * callback
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IAuthenticationProvider=_uuidof(IAuthenticationProvider);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationReceiverCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationReceiverCallback[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationReceiverCallback";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("ebf38c18-3f5d-4b79-9524-1c230dc69de8")] */
            MIDL_INTERFACE("ebf38c18-3f5d-4b79-9524-1c230dc69de8")
            IAuthenticationReceiverCallback : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE ValidationCompleted(
                    /* [in] */HSTRING token,
                    /* [in] */::boolean isValid
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Cancel(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IAuthenticationReceiverCallback=_uuidof(IAuthenticationReceiverCallback);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationReceiver
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationReceiver[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationReceiver";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("da1e721d-0de9-4dea-9760-5d218622eaeb")] */
            MIDL_INTERFACE("da1e721d-0de9-4dea-9760-5d218622eaeb")
            IAuthenticationReceiver : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE ValidateToken(
                    /* [in] */HSTRING token,
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationReceiverCallback * callback
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE GetRealm(
                    /* [out, retval] */HSTRING * result
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IAuthenticationReceiver=_uuidof(IAuthenticationReceiver);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__) */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.RecognizedSpeech
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version] */
            struct RecognizedSpeech
            {
                HSTRING RecognizedText;
                Windows::Media::SpeechRecognition::SpeechRecognitionConfidence Confidence;
                double RawConfidence;
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnRecognizedSpeechHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("46e4393b-301e-4f0c-b0fd-0d1f48090e6c")] */
            MIDL_INTERFACE("46e4393b-301e-4f0c-b0fd-0d1f48090e6c")
            IOnRecognizedSpeechHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */struct Microsoft::Holographic::AppRemoting::RecognizedSpeech recognizedSpeech
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnRecognizedSpeechHandler=_uuidof(IOnRecognizedSpeechHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteSpeech
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteSpeech[] = L"Microsoft.Holographic.AppRemoting.IRemoteSpeech";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("1a2b59d5-668f-41c1-b02b-6a7fb5433291")] */
            MIDL_INTERFACE("1a2b59d5-668f-41c1-b02b-6a7fb5433291")
            IRemoteSpeech : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE ApplyParameters(
                    /* [in] */HSTRING language,
                    /* [in] */Windows::Storage::IStorageFile * grammarFile,
                    /* [in] */unsigned int dictionaryLength,
                    /* [size_is(dictionaryLength), in] */HSTRING * dictionary
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnRecognizedSpeech(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnRecognizedSpeechHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnRecognizedSpeech(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IRemoteSpeech=_uuidof(IRemoteSpeech);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnConnectedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("bd1ba158-486e-4f9a-9a56-e0477174adbd")] */
            MIDL_INTERFACE("bd1ba158-486e-4f9a-9a56-e0477174adbd")
            IOnConnectedHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(void) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnConnectedHandler=_uuidof(IOnConnectedHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnDisconnectedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("b3b7ad12-f720-4a49-92c3-f825617330c5")] */
            MIDL_INTERFACE("b3b7ad12-f720-4a49-92c3-f825617330c5")
            IOnDisconnectedHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */Microsoft::Holographic::AppRemoting::ConnectionFailureReason failureReason
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnDisconnectedHandler=_uuidof(IOnDisconnectedHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnSendFrameHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("63858100-63d6-4509-b566-1eef31b35b77")] */
            MIDL_INTERFACE("63858100-63d6-4509-b566-1eef31b35b77")
            IOnSendFrameHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface * texture
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnSendFrameHandler=_uuidof(IOnSendFrameHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnDataChannelCreatedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("c3502d33-218b-42c8-8bfc-1790b357d8cb")] */
            MIDL_INTERFACE("c3502d33-218b-42c8-8bfc-1790b357d8cb")
            IOnDataChannelCreatedHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */Microsoft::Holographic::AppRemoting::IDataChannel * dataChannel,
                    /* [in] */::byte channelId
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnDataChannelCreatedHandler=_uuidof(IOnDataChannelCreatedHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnListeningHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [object, version, uuid("2ffb25e4-bf1c-403c-b2f8-69534c7ff11b")] */
            MIDL_INTERFACE("2ffb25e4-bf1c-403c-b2f8-69534c7ff11b")
            IOnListeningHandler : public IUnknown
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Invoke(
                    /* [in] */unsigned short port
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IOnListeningHandler=_uuidof(IOnListeningHandler);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__) */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            class RemoteContext;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */



/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteContext
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.RemoteContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteContext[] = L"Microsoft.Holographic.AppRemoting.IRemoteContext";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [uuid("eed1e1fe-ffe2-439a-a95c-70a58e6d7aa2"), version, object, exclusiveto] */
            MIDL_INTERFACE("eed1e1fe-ffe2-439a-a95c-70a58e6d7aa2")
            IRemoteContext : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Connect(
                    /* [in] */HSTRING hostname,
                    /* [in] */unsigned short port
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE ConnectSecure(
                    /* [in] */HSTRING hostname,
                    /* [in] */unsigned short port,
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationProvider * authProvider,
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateValidator * certValidator
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Listen(
                    /* [in] */HSTRING localHostname,
                    /* [in] */unsigned short handshakePort,
                    /* [in] */unsigned short primaryTransportPort
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE ListenSecure(
                    /* [in] */HSTRING localHostname,
                    /* [in] */unsigned short handshakePort,
                    /* [in] */unsigned short primaryTransportPort,
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateProvider * provider,
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationReceiver * receiver
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Disconnect(void) = 0;
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_ConnectionState(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::ConnectionState * value
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnConnected(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnConnectedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnConnected(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnDisconnected(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnDisconnectedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnDisconnected(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnListening(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnListeningHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnListening(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnSendFrame(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnSendFrameHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnSendFrame(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE CreateDataChannel(
                    /* [in] */::byte channelId,
                    /* [in] */Microsoft::Holographic::AppRemoting::DataChannelPriority priority
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnDataChannelCreated(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnDataChannelCreatedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnDataChannelCreated(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE GetRemoteSpeech(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IRemoteSpeech * * result
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IRemoteContext=_uuidof(IRemoteContext);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteContextStatics
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.RemoteContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteContextStatics[] = L"Microsoft.Holographic.AppRemoting.IRemoteContextStatics";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [uuid("ab97c9ab-4f28-4a07-bbc9-eff7da9ec640"), version, object, exclusiveto] */
            MIDL_INTERFACE("ab97c9ab-4f28-4a07-bbc9-eff7da9ec640")
            IRemoteContextStatics : public IInspectable
            {
            public:
                /* [overload] */virtual HRESULT STDMETHODCALLTYPE Create(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IRemoteContext * * result
                    ) = 0;
                /* [overload] */virtual HRESULT STDMETHODCALLTYPE Create2(
                    /* [in] */unsigned int maxBitrateKbps,
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IRemoteContext * * result
                    ) = 0;
                /* [overload] */virtual HRESULT STDMETHODCALLTYPE Create3(
                    /* [in] */unsigned int maxBitrateKbps,
                    /* [in] */::boolean enableAudio,
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IRemoteContext * * result
                    ) = 0;
                /* [overload] */virtual HRESULT STDMETHODCALLTYPE Create4(
                    /* [in] */unsigned int maxBitrateKbps,
                    /* [in] */::boolean enableAudio,
                    /* [in] */Microsoft::Holographic::AppRemoting::PreferredVideoCodec preferredVideoCodec,
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IRemoteContext * * result
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IRemoteContextStatics=_uuidof(IRemoteContextStatics);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__) */


/*
 *
 * Class Microsoft.Holographic.AppRemoting.RemoteContext
 *
 * RuntimeClass contains static methods.
 *
 * Class implements the following interfaces:
 *    Microsoft.Holographic.AppRemoting.IRemoteContext ** Default Interface **
 *    Windows.Foundation.IClosable
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Microsoft_Holographic_AppRemoting_RemoteContext_DEFINED
#define RUNTIMECLASS_Microsoft_Holographic_AppRemoting_RemoteContext_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Microsoft_Holographic_AppRemoting_RemoteContext[] = L"Microsoft.Holographic.AppRemoting.RemoteContext";
#endif


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.BlitResult
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version, v1_enum] */
            enum BlitResult : int
            {
                BlitResult_Success_Color,
                BlitResult_Failed_NoRemoteFrameAvailable,
                BlitResult_Failed_NoCamera,
                BlitResult_Failed_RemoteFrameTooOld,
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.PlayerFrameStatistics
 *
 */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [version] */
            struct PlayerFrameStatistics
            {
                float Latency;
                float TimeSinceLastPresent;
                unsigned int VideoFrameReusedCount;
                unsigned int VideoFramesSkipped;
                unsigned int VideoFramesReceived;
                unsigned int VideoFramesDiscarded;
                float VideoFrameMinDelta;
                float VideoFrameMaxDelta;
            };
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */


namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            class PlayerContext;
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */



/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContext2
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContext2[] = L"Microsoft.Holographic.AppRemoting.IPlayerContext2";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [uuid("529862bd-39af-4b55-980f-46bbdb9854b2"), version, object, exclusiveto] */
            MIDL_INTERFACE("529862bd-39af-4b55-980f-46bbdb9854b2")
            IPlayerContext2 : public IInspectable
            {
            public:
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_BlitRemoteFrameTimeout(
                    /* [out, retval] */struct Windows::Foundation::TimeSpan * value
                    ) = 0;
                /* [propput] */virtual HRESULT STDMETHODCALLTYPE put_BlitRemoteFrameTimeout(
                    /* [in] */struct Windows::Foundation::TimeSpan value
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IPlayerContext2=_uuidof(IPlayerContext2);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContext
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContext[] = L"Microsoft.Holographic.AppRemoting.IPlayerContext";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [uuid("f1a6f630-4427-429b-82ba-9c87f65dafe8"), version, object, exclusiveto] */
            MIDL_INTERFACE("f1a6f630-4427-429b-82ba-9c87f65dafe8")
            IPlayerContext : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Connect(
                    /* [in] */HSTRING hostname,
                    /* [in] */unsigned short port
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE ConnectSecure(
                    /* [in] */HSTRING hostname,
                    /* [in] */unsigned short port,
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationProvider * authProvider,
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateValidator * certValidator
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Listen(
                    /* [in] */HSTRING localHostname,
                    /* [in] */unsigned short handshakePort,
                    /* [in] */unsigned short primaryTransportPort
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE ListenSecure(
                    /* [in] */HSTRING localHostname,
                    /* [in] */unsigned short handshakePort,
                    /* [in] */unsigned short primaryTransportPort,
                    /* [in] */Microsoft::Holographic::AppRemoting::ICertificateProvider * provider,
                    /* [in] */Microsoft::Holographic::AppRemoting::IAuthenticationReceiver * receiver
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE Disconnect(void) = 0;
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_ConnectionState(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::ConnectionState * value
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnConnected(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnConnectedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnConnected(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnDisconnected(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnDisconnectedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnDisconnected(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnListening(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnListeningHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnListening(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE BlitRemoteFrame(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::BlitResult * result
                    ) = 0;
                virtual HRESULT STDMETHODCALLTYPE CreateDataChannel(
                    /* [in] */::byte channelId,
                    /* [in] */Microsoft::Holographic::AppRemoting::DataChannelPriority priority
                    ) = 0;
                /* [eventadd] */virtual HRESULT STDMETHODCALLTYPE add_OnDataChannelCreated(
                    /* [in] */Microsoft::Holographic::AppRemoting::IOnDataChannelCreatedHandler  * handler,
                    /* [retval, out] */EventRegistrationToken * token
                    ) = 0;
                /* [eventremove] */virtual HRESULT STDMETHODCALLTYPE remove_OnDataChannelCreated(
                    /* [in] */EventRegistrationToken token
                    ) = 0;
                /* [propget] */virtual HRESULT STDMETHODCALLTYPE get_LastFrameStatistics(
                    /* [out, retval] */struct Microsoft::Holographic::AppRemoting::PlayerFrameStatistics * value
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IPlayerContext=_uuidof(IPlayerContext);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContextStatics
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContextStatics[] = L"Microsoft.Holographic.AppRemoting.IPlayerContextStatics";

namespace Microsoft {
    namespace Holographic {
        namespace AppRemoting {
            /* [uuid("dfae5c25-4f8a-4980-b670-a6811488cd37"), version, object, exclusiveto] */
            MIDL_INTERFACE("dfae5c25-4f8a-4980-b670-a6811488cd37")
            IPlayerContextStatics : public IInspectable
            {
            public:
                virtual HRESULT STDMETHODCALLTYPE Create(
                    /* [out, retval] */Microsoft::Holographic::AppRemoting::IPlayerContext * * result
                    ) = 0;
                
            };

            extern MIDL_CONST_ID IID & IID_IPlayerContextStatics=_uuidof(IPlayerContextStatics);
            
        } /* AppRemoting */
    } /* Holographic */
} /* Microsoft */

EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__) */


/*
 *
 * Class Microsoft.Holographic.AppRemoting.PlayerContext
 *
 * RuntimeClass contains static methods.
 *
 * Class implements the following interfaces:
 *    Microsoft.Holographic.AppRemoting.IPlayerContext2
 *    Microsoft.Holographic.AppRemoting.IPlayerContext ** Default Interface **
 *    Windows.Foundation.IClosable
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Microsoft_Holographic_AppRemoting_PlayerContext_DEFINED
#define RUNTIMECLASS_Microsoft_Holographic_AppRemoting_PlayerContext_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Microsoft_Holographic_AppRemoting_PlayerContext[] = L"Microsoft.Holographic.AppRemoting.PlayerContext";
#endif




#else // !defined(__cplusplus)
/* Forward Declarations */
#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificate __x_Microsoft_CHolographic_CAppRemoting_CICertificate;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_FWD_DEFINED__

#ifndef ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__
typedef interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics;

#endif // ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_FWD_DEFINED__

// Parameterized interface forward declarations (C)

// Collection interface definitions
#if !defined(____FIReference_1_boolean_INTERFACE_DEFINED__)
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

typedef interface __FIReference_1_boolean __FIReference_1_boolean;

//  Declare the parameterized interface IID.
EXTERN_C const IID IID___FIReference_1_boolean;

typedef struct __FIReference_1_booleanVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(__RPC__in __FIReference_1_boolean * This,
        /* [in] */ __RPC__in REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    ULONG ( STDMETHODCALLTYPE *AddRef )( __RPC__in __FIReference_1_boolean * This );
    ULONG ( STDMETHODCALLTYPE *Release )( __RPC__in __FIReference_1_boolean * This );

    HRESULT ( STDMETHODCALLTYPE *GetIids )( __RPC__in __FIReference_1_boolean * This, 
                                            /* [out] */ __RPC__out ULONG *iidCount,
                                            /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids);
    HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )( __RPC__in __FIReference_1_boolean * This, /* [out] */ __RPC__deref_out_opt HSTRING *className);
    HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )( __RPC__in __FIReference_1_boolean * This, /* [out] */ __RPC__out TrustLevel *trustLevel);

    /* [propget] */ HRESULT ( STDMETHODCALLTYPE *get_Value )(__RPC__in __FIReference_1_boolean * This, /* [retval][out] */ __RPC__out boolean *value);
    END_INTERFACE
} __FIReference_1_booleanVtbl;

interface __FIReference_1_boolean
{
    CONST_VTBL struct __FIReference_1_booleanVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __FIReference_1_boolean_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 
#define __FIReference_1_boolean_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 
#define __FIReference_1_boolean_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 

#define __FIReference_1_boolean_GetIids(This,iidCount,iids)	\
    ( (This)->lpVtbl -> GetIids(This,iidCount,iids) ) 
#define __FIReference_1_boolean_GetRuntimeClassName(This,className)	\
    ( (This)->lpVtbl -> GetRuntimeClassName(This,className) ) 
#define __FIReference_1_boolean_GetTrustLevel(This,trustLevel)	\
    ( (This)->lpVtbl -> GetTrustLevel(This,trustLevel) ) 

#define __FIReference_1_boolean_get_Value(This,value)	\
    ( (This)->lpVtbl -> get_Value(This,value) ) 
#endif /* COBJMACROS */


#endif // ____FIReference_1_boolean_INTERFACE_DEFINED__


struct __x_Microsoft_CHolographic_CAppRemoting_CCertificateValidationResult;

#if !defined(____FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_INTERFACE_DEFINED__)
#define ____FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_INTERFACE_DEFINED__

typedef interface __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult;

//  Declare the parameterized interface IID.
EXTERN_C const IID IID___FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult;

typedef struct __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResultVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(__RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This,
        /* [in] */ __RPC__in REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    ULONG ( STDMETHODCALLTYPE *AddRef )( __RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This );
    ULONG ( STDMETHODCALLTYPE *Release )( __RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This );

    HRESULT ( STDMETHODCALLTYPE *GetIids )( __RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This, 
                                            /* [out] */ __RPC__out ULONG *iidCount,
                                            /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids);
    HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )( __RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This, /* [out] */ __RPC__deref_out_opt HSTRING *className);
    HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )( __RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This, /* [out] */ __RPC__out TrustLevel *trustLevel);

    /* [propget] */ HRESULT ( STDMETHODCALLTYPE *get_Value )(__RPC__in __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * This, /* [retval][out] */ __RPC__out struct __x_Microsoft_CHolographic_CAppRemoting_CCertificateValidationResult *value);
    END_INTERFACE
} __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResultVtbl;

interface __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult
{
    CONST_VTBL struct __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResultVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 

#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_GetIids(This,iidCount,iids)	\
    ( (This)->lpVtbl -> GetIids(This,iidCount,iids) ) 
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_GetRuntimeClassName(This,className)	\
    ( (This)->lpVtbl -> GetRuntimeClassName(This,className) ) 
#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_GetTrustLevel(This,trustLevel)	\
    ( (This)->lpVtbl -> GetTrustLevel(This,trustLevel) ) 

#define __FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_get_Value(This,value)	\
    ( (This)->lpVtbl -> get_Value(This,value) ) 
#endif /* COBJMACROS */


#endif // ____FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult_INTERFACE_DEFINED__




/*
 *
 * Struct Microsoft.Holographic.AppRemoting.ConnectionState
 *
 */

/* [version, v1_enum] */
enum __x_Microsoft_CHolographic_CAppRemoting_CConnectionState
{
    ConnectionState_Disconnected,
    ConnectionState_Connecting,
    ConnectionState_Connected,
};


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.ConnectionFailureReason
 *
 */

/* [version, v1_enum] */
enum __x_Microsoft_CHolographic_CAppRemoting_CConnectionFailureReason
{
    ConnectionFailureReason_None,
    ConnectionFailureReason_Unknown,
    ConnectionFailureReason_NoServerCertificate,
    ConnectionFailureReason_HandshakePortBusy,
    ConnectionFailureReason_HandshakeUnreachable,
    ConnectionFailureReason_HandshakeConnectionFailed,
    ConnectionFailureReason_AuthenticationFailed,
    ConnectionFailureReason_RemotingVersionMismatch,
    ConnectionFailureReason_IncompatibleTransportProtocols,
    ConnectionFailureReason_HandshakeFailed,
    ConnectionFailureReason_TransportPortBusy,
    ConnectionFailureReason_TransportUnreachable,
    ConnectionFailureReason_TransportConnectionFailed,
    ConnectionFailureReason_ProtocolVersionMismatch,
    ConnectionFailureReason_ProtocolError,
    ConnectionFailureReason_VideoCodecNotAvailable,
    ConnectionFailureReason_Canceled,
    ConnectionFailureReason_ConnectionLost,
    ConnectionFailureReason_DeviceLost,
    ConnectionFailureReason_DisconnectRequest,
};


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.PreferredVideoCodec
 *
 */

/* [version, v1_enum] */
enum __x_Microsoft_CHolographic_CAppRemoting_CPreferredVideoCodec
{
    PreferredVideoCodec_Default,
    PreferredVideoCodec_H264,
    PreferredVideoCodec_H265,
};


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.DataChannelPriority
 *
 */

/* [version, v1_enum] */
enum __x_Microsoft_CHolographic_CAppRemoting_CDataChannelPriority
{
    DataChannelPriority_Low,
    DataChannelPriority_Medium,
    DataChannelPriority_High,
};


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.DataChannelReceivedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__
/* [object, version, uuid("c972e7dd-da67-4815-b3d4-9828df3a045c")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler * This,
        /* [in] */unsigned int dataLength,
        /* [size_is(dataLength), in] */byte * data
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_Invoke(This,dataLength,data) \
    ( (This)->lpVtbl->Invoke(This,dataLength,data) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.DataChannelCloseHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__
/* [object, version, uuid("77467e89-a9fd-4d8d-aea2-bfbba50a32a7")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_Invoke(This) \
    ( (This)->lpVtbl->Invoke(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IDataChannel
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IDataChannel[] = L"Microsoft.Holographic.AppRemoting.IDataChannel";
/* [object, version, uuid("66b1e9f7-5ece-47f5-b783-91dacaf06188")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
/* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnDataReceived )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIDataChannelReceivedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnDataReceived )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
        /* [in] */EventRegistrationToken token
        );
    HRESULT ( STDMETHODCALLTYPE *SendData )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
        /* [in] */unsigned int dataLength,
        /* [size_is(dataLength), in] */byte * data,
        /* [in] */boolean guaranteedDelivery
        );
    HRESULT ( STDMETHODCALLTYPE *Close )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnClosed )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIDataChannelCloseHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnClosed )(
        __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * This,
        /* [in] */EventRegistrationToken token
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIDataChannelVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_add_OnDataReceived(This,handler,token) \
    ( (This)->lpVtbl->add_OnDataReceived(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_remove_OnDataReceived(This,token) \
    ( (This)->lpVtbl->remove_OnDataReceived(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_SendData(This,dataLength,data,guaranteedDelivery) \
    ( (This)->lpVtbl->SendData(This,dataLength,data,guaranteedDelivery) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_Close(This) \
    ( (This)->lpVtbl->Close(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_add_OnClosed(This,handler,token) \
    ( (This)->lpVtbl->add_OnClosed(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_remove_OnClosed(This,token) \
    ( (This)->lpVtbl->remove_OnClosed(This,token) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIDataChannel;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIDataChannel_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificate
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificate[] = L"Microsoft.Holographic.AppRemoting.ICertificate";
/* [object, version, uuid("a0669db2-2157-4227-8800-0a357c2a2900")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *GetCertificatePfx )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
        /* [out] */unsigned int * resultLength,
        /* [size_is(, *(resultLength)), retval, out] */byte * * result
        );
    HRESULT ( STDMETHODCALLTYPE *GetSubjectName )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
        /* [out, retval] */HSTRING * result
        );
    HRESULT ( STDMETHODCALLTYPE *GetPfxPassword )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificate * This,
        /* [out, retval] */HSTRING * result
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificate
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetCertificatePfx(This,resultLength,result) \
    ( (This)->lpVtbl->GetCertificatePfx(This,resultLength,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetSubjectName(This,result) \
    ( (This)->lpVtbl->GetSubjectName(This,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificate_GetPfxPassword(This,result) \
    ( (This)->lpVtbl->GetPfxPassword(This,result) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificate;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificate_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateProviderCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateProviderCallback[] = L"Microsoft.Holographic.AppRemoting.ICertificateProviderCallback";
/* [object, version, uuid("6797f815-e529-4697-b61d-68ef5082ad19")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallbackVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *CertificateReceived )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificate * certificate
        );
    HRESULT ( STDMETHODCALLTYPE *Cancel )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallbackVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallbackVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_CertificateReceived(This,certificate) \
    ( (This)->lpVtbl->CertificateReceived(This,certificate) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_Cancel(This) \
    ( (This)->lpVtbl->Cancel(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateProvider
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateProvider[] = L"Microsoft.Holographic.AppRemoting.ICertificateProvider";
/* [object, version, uuid("eaf80ccd-2d8a-4f43-a64f-ba1f91bdc25f")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *GetCertificate )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderCallback * callback
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateProviderVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_GetCertificate(This,callback) \
    ( (This)->lpVtbl->GetCertificate(This,callback) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateChain
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateChain[] = L"Microsoft.Holographic.AppRemoting.ICertificateChain";
/* [object, version, uuid("4415b01f-ce8f-4599-a7c8-0b5e6a277708")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateChainVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *GetCertificate )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
        /* [in] */unsigned int index,
        /* [out] */unsigned int * resultLength,
        /* [size_is(, *(resultLength)), retval, out] */byte * * result
        );
    /* [propget] */HRESULT ( STDMETHODCALLTYPE *get_Length )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * This,
        /* [out, retval] */unsigned int * value
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateChainVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateChainVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_GetCertificate(This,index,resultLength,result) \
    ( (This)->lpVtbl->GetCertificate(This,index,resultLength,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_get_Length(This,value) \
    ( (This)->lpVtbl->get_Length(This,value) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateChain;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateChain_INTERFACE_DEFINED__) */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.CertificateValidationResult
 *
 */

/* [version] */
struct __x_Microsoft_CHolographic_CAppRemoting_CCertificateValidationResult
{
    boolean TrustedRoot;
    boolean Revoked;
    boolean Expired;
    boolean WrongUsage;
    __FIReference_1_boolean * NameMismatch;
    boolean RevocationCheckFailed;
    boolean InvalidCertOrChain;
};


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateValidationCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateValidationCallback[] = L"Microsoft.Holographic.AppRemoting.ICertificateValidationCallback";
/* [object, version, uuid("c8945a4c-33db-4143-8c9e-af35a2e57809")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallbackVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *CertificateValidated )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This,
        /* [in] */struct __x_Microsoft_CHolographic_CAppRemoting_CCertificateValidationResult result
        );
    HRESULT ( STDMETHODCALLTYPE *Cancel )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallbackVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallbackVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_CertificateValidated(This,result) \
    ( (This)->lpVtbl->CertificateValidated(This,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_Cancel(This) \
    ( (This)->lpVtbl->Cancel(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.ICertificateValidator
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_ICertificateValidator[] = L"Microsoft.Holographic.AppRemoting.ICertificateValidator";
/* [object, version, uuid("ac9bd062-c81e-479f-b33c-ac979c6712e5")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidatorVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *ValidateCertificate )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateChain * certificateChain,
        /* [in] */HSTRING serverName,
        /* [in] */boolean forceRevocationCheck,
        /* [in] */__FIReference_1_Microsoft__CHolographic__CAppRemoting__CCertificateValidationResult * systemValidationResult,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateValidationCallback * callback
        );
    /* [propget] */HRESULT ( STDMETHODCALLTYPE *get_PerformSystemValidation )(
        __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * This,
        /* [out, retval] */boolean * value
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidatorVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidatorVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_ValidateCertificate(This,certificateChain,serverName,forceRevocationCheck,systemValidationResult,callback) \
    ( (This)->lpVtbl->ValidateCertificate(This,certificateChain,serverName,forceRevocationCheck,systemValidationResult,callback) )

#define __x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_get_PerformSystemValidation(This,value) \
    ( (This)->lpVtbl->get_PerformSystemValidation(This,value) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationProviderCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationProviderCallback[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationProviderCallback";
/* [object, version, uuid("c4bc488f-8cd4-4118-ac15-7859f3916676")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallbackVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *TokenReceived )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This,
        /* [in] */HSTRING token
        );
    HRESULT ( STDMETHODCALLTYPE *Cancel )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallbackVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallbackVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_TokenReceived(This,token) \
    ( (This)->lpVtbl->TokenReceived(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_Cancel(This) \
    ( (This)->lpVtbl->Cancel(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationProvider
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationProvider[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationProvider";
/* [object, version, uuid("af3d4cd6-8484-445a-b8b3-b96da5711e3e")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *GetToken )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderCallback * callback
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProviderVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_GetToken(This,callback) \
    ( (This)->lpVtbl->GetToken(This,callback) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationReceiverCallback
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationReceiverCallback[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationReceiverCallback";
/* [object, version, uuid("ebf38c18-3f5d-4b79-9524-1c230dc69de8")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallbackVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *ValidationCompleted )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This,
        /* [in] */HSTRING token,
        /* [in] */boolean isValid
        );
    HRESULT ( STDMETHODCALLTYPE *Cancel )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallbackVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallbackVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_ValidationCompleted(This,token,isValid) \
    ( (This)->lpVtbl->ValidationCompleted(This,token,isValid) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_Cancel(This) \
    ( (This)->lpVtbl->Cancel(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IAuthenticationReceiver
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IAuthenticationReceiver[] = L"Microsoft.Holographic.AppRemoting.IAuthenticationReceiver";
/* [object, version, uuid("da1e721d-0de9-4dea-9760-5d218622eaeb")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *ValidateToken )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
        /* [in] */HSTRING token,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverCallback * callback
        );
    HRESULT ( STDMETHODCALLTYPE *GetRealm )(
        __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * This,
        /* [out, retval] */HSTRING * result
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiverVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_ValidateToken(This,token,callback) \
    ( (This)->lpVtbl->ValidateToken(This,token,callback) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_GetRealm(This,result) \
    ( (This)->lpVtbl->GetRealm(This,result) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver_INTERFACE_DEFINED__) */


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.RecognizedSpeech
 *
 */

/* [version] */
struct __x_Microsoft_CHolographic_CAppRemoting_CRecognizedSpeech
{
    HSTRING RecognizedText;
    enum __x_Windows_CMedia_CSpeechRecognition_CSpeechRecognitionConfidence Confidence;
    double RawConfidence;
};


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnRecognizedSpeechHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__
/* [object, version, uuid("46e4393b-301e-4f0c-b0fd-0d1f48090e6c")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler * This,
        /* [in] */struct __x_Microsoft_CHolographic_CAppRemoting_CRecognizedSpeech recognizedSpeech
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_Invoke(This,recognizedSpeech) \
    ( (This)->lpVtbl->Invoke(This,recognizedSpeech) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteSpeech
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteSpeech[] = L"Microsoft.Holographic.AppRemoting.IRemoteSpeech";
/* [object, version, uuid("1a2b59d5-668f-41c1-b02b-6a7fb5433291")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeechVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *ApplyParameters )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
        /* [in] */HSTRING language,
        /* [in] */__x_Windows_CStorage_CIStorageFile * grammarFile,
        /* [in] */unsigned int dictionaryLength,
        /* [size_is(dictionaryLength), in] */HSTRING * dictionary
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnRecognizedSpeech )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnRecognizedSpeechHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnRecognizedSpeech )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * This,
        /* [in] */EventRegistrationToken token
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeechVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeechVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_ApplyParameters(This,language,grammarFile,dictionaryLength,dictionary) \
    ( (This)->lpVtbl->ApplyParameters(This,language,grammarFile,dictionaryLength,dictionary) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_add_OnRecognizedSpeech(This,handler,token) \
    ( (This)->lpVtbl->add_OnRecognizedSpeech(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_remove_OnRecognizedSpeech(This,token) \
    ( (This)->lpVtbl->remove_OnRecognizedSpeech(This,token) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnConnectedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__
/* [object, version, uuid("bd1ba158-486e-4f9a-9a56-e0477174adbd")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler * This
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_Invoke(This) \
    ( (This)->lpVtbl->Invoke(This) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnDisconnectedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__
/* [object, version, uuid("b3b7ad12-f720-4a49-92c3-f825617330c5")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler * This,
        /* [in] */enum __x_Microsoft_CHolographic_CAppRemoting_CConnectionFailureReason failureReason
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_Invoke(This,failureReason) \
    ( (This)->lpVtbl->Invoke(This,failureReason) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnSendFrameHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__
/* [object, version, uuid("63858100-63d6-4509-b566-1eef31b35b77")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler * This,
        /* [in] */__x_Windows_CGraphics_CDirectX_CDirect3D11_CIDirect3DSurface * texture
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_Invoke(This,texture) \
    ( (This)->lpVtbl->Invoke(This,texture) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnDataChannelCreatedHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__
/* [object, version, uuid("c3502d33-218b-42c8-8bfc-1790b357d8cb")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIDataChannel * dataChannel,
        /* [in] */byte channelId
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_Invoke(This,dataChannel,channelId) \
    ( (This)->lpVtbl->Invoke(This,dataChannel,channelId) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler_INTERFACE_DEFINED__) */


/*
 *
 * Delegate Microsoft.Holographic.AppRemoting.OnListeningHandler
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__
/* [object, version, uuid("2ffb25e4-bf1c-403c-b2f8-69534c7ff11b")] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandlerVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler * This);

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler * This);
HRESULT ( STDMETHODCALLTYPE *Invoke )(
        __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler * This,
        /* [in] */unsigned short port
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandlerVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandlerVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_Invoke(This,port) \
    ( (This)->lpVtbl->Invoke(This,port) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler_INTERFACE_DEFINED__) */



/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteContext
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.RemoteContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteContext[] = L"Microsoft.Holographic.AppRemoting.IRemoteContext";
/* [uuid("eed1e1fe-ffe2-439a-a95c-70a58e6d7aa2"), version, object, exclusiveto] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *Connect )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */HSTRING hostname,
        /* [in] */unsigned short port
        );
    HRESULT ( STDMETHODCALLTYPE *ConnectSecure )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */HSTRING hostname,
        /* [in] */unsigned short port,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * authProvider,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * certValidator
        );
    HRESULT ( STDMETHODCALLTYPE *Listen )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */HSTRING localHostname,
        /* [in] */unsigned short handshakePort,
        /* [in] */unsigned short primaryTransportPort
        );
    HRESULT ( STDMETHODCALLTYPE *ListenSecure )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */HSTRING localHostname,
        /* [in] */unsigned short handshakePort,
        /* [in] */unsigned short primaryTransportPort,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * provider,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * receiver
        );
    HRESULT ( STDMETHODCALLTYPE *Disconnect )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This
        );
    /* [propget] */HRESULT ( STDMETHODCALLTYPE *get_ConnectionState )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [out, retval] */enum __x_Microsoft_CHolographic_CAppRemoting_CConnectionState * value
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnConnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnConnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnDisconnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnDisconnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnListening )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnListening )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnSendFrame )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnSendFrameHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnSendFrame )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */EventRegistrationToken token
        );
    HRESULT ( STDMETHODCALLTYPE *CreateDataChannel )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */byte channelId,
        /* [in] */enum __x_Microsoft_CHolographic_CAppRemoting_CDataChannelPriority priority
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnDataChannelCreated )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnDataChannelCreated )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [in] */EventRegistrationToken token
        );
    HRESULT ( STDMETHODCALLTYPE *GetRemoteSpeech )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * This,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIRemoteSpeech * * result
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_Connect(This,hostname,port) \
    ( (This)->lpVtbl->Connect(This,hostname,port) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_ConnectSecure(This,hostname,port,authProvider,certValidator) \
    ( (This)->lpVtbl->ConnectSecure(This,hostname,port,authProvider,certValidator) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_Listen(This,localHostname,handshakePort,primaryTransportPort) \
    ( (This)->lpVtbl->Listen(This,localHostname,handshakePort,primaryTransportPort) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_ListenSecure(This,localHostname,handshakePort,primaryTransportPort,provider,receiver) \
    ( (This)->lpVtbl->ListenSecure(This,localHostname,handshakePort,primaryTransportPort,provider,receiver) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_Disconnect(This) \
    ( (This)->lpVtbl->Disconnect(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_get_ConnectionState(This,value) \
    ( (This)->lpVtbl->get_ConnectionState(This,value) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_add_OnConnected(This,handler,token) \
    ( (This)->lpVtbl->add_OnConnected(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_remove_OnConnected(This,token) \
    ( (This)->lpVtbl->remove_OnConnected(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_add_OnDisconnected(This,handler,token) \
    ( (This)->lpVtbl->add_OnDisconnected(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_remove_OnDisconnected(This,token) \
    ( (This)->lpVtbl->remove_OnDisconnected(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_add_OnListening(This,handler,token) \
    ( (This)->lpVtbl->add_OnListening(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_remove_OnListening(This,token) \
    ( (This)->lpVtbl->remove_OnListening(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_add_OnSendFrame(This,handler,token) \
    ( (This)->lpVtbl->add_OnSendFrame(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_remove_OnSendFrame(This,token) \
    ( (This)->lpVtbl->remove_OnSendFrame(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_CreateDataChannel(This,channelId,priority) \
    ( (This)->lpVtbl->CreateDataChannel(This,channelId,priority) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_add_OnDataChannelCreated(This,handler,token) \
    ( (This)->lpVtbl->add_OnDataChannelCreated(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_remove_OnDataChannelCreated(This,token) \
    ( (This)->lpVtbl->remove_OnDataChannelCreated(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_GetRemoteSpeech(This,result) \
    ( (This)->lpVtbl->GetRemoteSpeech(This,result) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IRemoteContextStatics
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.RemoteContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IRemoteContextStatics[] = L"Microsoft.Holographic.AppRemoting.IRemoteContextStatics";
/* [uuid("ab97c9ab-4f28-4a07-bbc9-eff7da9ec640"), version, object, exclusiveto] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStaticsVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
/* [overload] */HRESULT ( STDMETHODCALLTYPE *Create )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * * result
        );
    /* [overload] */HRESULT ( STDMETHODCALLTYPE *Create2 )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
        /* [in] */unsigned int maxBitrateKbps,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * * result
        );
    /* [overload] */HRESULT ( STDMETHODCALLTYPE *Create3 )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
        /* [in] */unsigned int maxBitrateKbps,
        /* [in] */boolean enableAudio,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * * result
        );
    /* [overload] */HRESULT ( STDMETHODCALLTYPE *Create4 )(
        __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics * This,
        /* [in] */unsigned int maxBitrateKbps,
        /* [in] */boolean enableAudio,
        /* [in] */enum __x_Microsoft_CHolographic_CAppRemoting_CPreferredVideoCodec preferredVideoCodec,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIRemoteContext * * result
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStaticsVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStaticsVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_Create(This,result) \
    ( (This)->lpVtbl->Create(This,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_Create2(This,maxBitrateKbps,result) \
    ( (This)->lpVtbl->Create2(This,maxBitrateKbps,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_Create3(This,maxBitrateKbps,enableAudio,result) \
    ( (This)->lpVtbl->Create3(This,maxBitrateKbps,enableAudio,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_Create4(This,maxBitrateKbps,enableAudio,preferredVideoCodec,result) \
    ( (This)->lpVtbl->Create4(This,maxBitrateKbps,enableAudio,preferredVideoCodec,result) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIRemoteContextStatics_INTERFACE_DEFINED__) */


/*
 *
 * Class Microsoft.Holographic.AppRemoting.RemoteContext
 *
 * RuntimeClass contains static methods.
 *
 * Class implements the following interfaces:
 *    Microsoft.Holographic.AppRemoting.IRemoteContext ** Default Interface **
 *    Windows.Foundation.IClosable
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Microsoft_Holographic_AppRemoting_RemoteContext_DEFINED
#define RUNTIMECLASS_Microsoft_Holographic_AppRemoting_RemoteContext_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Microsoft_Holographic_AppRemoting_RemoteContext[] = L"Microsoft.Holographic.AppRemoting.RemoteContext";
#endif


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.BlitResult
 *
 */

/* [version, v1_enum] */
enum __x_Microsoft_CHolographic_CAppRemoting_CBlitResult
{
    BlitResult_Success_Color,
    BlitResult_Failed_NoRemoteFrameAvailable,
    BlitResult_Failed_NoCamera,
    BlitResult_Failed_RemoteFrameTooOld,
};


/*
 *
 * Struct Microsoft.Holographic.AppRemoting.PlayerFrameStatistics
 *
 */

/* [version] */
struct __x_Microsoft_CHolographic_CAppRemoting_CPlayerFrameStatistics
{
    float Latency;
    float TimeSinceLastPresent;
    unsigned int VideoFrameReusedCount;
    unsigned int VideoFramesSkipped;
    unsigned int VideoFramesReceived;
    unsigned int VideoFramesDiscarded;
    float VideoFrameMinDelta;
    float VideoFrameMaxDelta;
};



/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContext2
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContext2[] = L"Microsoft.Holographic.AppRemoting.IPlayerContext2";
/* [uuid("529862bd-39af-4b55-980f-46bbdb9854b2"), version, object, exclusiveto] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2Vtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
/* [propget] */HRESULT ( STDMETHODCALLTYPE *get_BlitRemoteFrameTimeout )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
        /* [out, retval] */struct __x_Windows_CFoundation_CTimeSpan * value
        );
    /* [propput] */HRESULT ( STDMETHODCALLTYPE *put_BlitRemoteFrameTimeout )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2 * This,
        /* [in] */struct __x_Windows_CFoundation_CTimeSpan value
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2Vtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2Vtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_get_BlitRemoteFrameTimeout(This,value) \
    ( (This)->lpVtbl->get_BlitRemoteFrameTimeout(This,value) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_put_BlitRemoteFrameTimeout(This,value) \
    ( (This)->lpVtbl->put_BlitRemoteFrameTimeout(This,value) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext2_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContext
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContext[] = L"Microsoft.Holographic.AppRemoting.IPlayerContext";
/* [uuid("f1a6f630-4427-429b-82ba-9c87f65dafe8"), version, object, exclusiveto] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *Connect )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */HSTRING hostname,
        /* [in] */unsigned short port
        );
    HRESULT ( STDMETHODCALLTYPE *ConnectSecure )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */HSTRING hostname,
        /* [in] */unsigned short port,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationProvider * authProvider,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateValidator * certValidator
        );
    HRESULT ( STDMETHODCALLTYPE *Listen )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */HSTRING localHostname,
        /* [in] */unsigned short handshakePort,
        /* [in] */unsigned short primaryTransportPort
        );
    HRESULT ( STDMETHODCALLTYPE *ListenSecure )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */HSTRING localHostname,
        /* [in] */unsigned short handshakePort,
        /* [in] */unsigned short primaryTransportPort,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CICertificateProvider * provider,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIAuthenticationReceiver * receiver
        );
    HRESULT ( STDMETHODCALLTYPE *Disconnect )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This
        );
    /* [propget] */HRESULT ( STDMETHODCALLTYPE *get_ConnectionState )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [out, retval] */enum __x_Microsoft_CHolographic_CAppRemoting_CConnectionState * value
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnConnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnConnectedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnConnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnDisconnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnDisconnectedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnDisconnected )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnListening )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnListeningHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnListening )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */EventRegistrationToken token
        );
    HRESULT ( STDMETHODCALLTYPE *BlitRemoteFrame )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [out, retval] */enum __x_Microsoft_CHolographic_CAppRemoting_CBlitResult * result
        );
    HRESULT ( STDMETHODCALLTYPE *CreateDataChannel )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */byte channelId,
        /* [in] */enum __x_Microsoft_CHolographic_CAppRemoting_CDataChannelPriority priority
        );
    /* [eventadd] */HRESULT ( STDMETHODCALLTYPE *add_OnDataChannelCreated )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */__x_Microsoft_CHolographic_CAppRemoting_CIOnDataChannelCreatedHandler  * handler,
        /* [retval, out] */EventRegistrationToken * token
        );
    /* [eventremove] */HRESULT ( STDMETHODCALLTYPE *remove_OnDataChannelCreated )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [in] */EventRegistrationToken token
        );
    /* [propget] */HRESULT ( STDMETHODCALLTYPE *get_LastFrameStatistics )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * This,
        /* [out, retval] */struct __x_Microsoft_CHolographic_CAppRemoting_CPlayerFrameStatistics * value
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_Connect(This,hostname,port) \
    ( (This)->lpVtbl->Connect(This,hostname,port) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_ConnectSecure(This,hostname,port,authProvider,certValidator) \
    ( (This)->lpVtbl->ConnectSecure(This,hostname,port,authProvider,certValidator) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_Listen(This,localHostname,handshakePort,primaryTransportPort) \
    ( (This)->lpVtbl->Listen(This,localHostname,handshakePort,primaryTransportPort) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_ListenSecure(This,localHostname,handshakePort,primaryTransportPort,provider,receiver) \
    ( (This)->lpVtbl->ListenSecure(This,localHostname,handshakePort,primaryTransportPort,provider,receiver) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_Disconnect(This) \
    ( (This)->lpVtbl->Disconnect(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_get_ConnectionState(This,value) \
    ( (This)->lpVtbl->get_ConnectionState(This,value) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_add_OnConnected(This,handler,token) \
    ( (This)->lpVtbl->add_OnConnected(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_remove_OnConnected(This,token) \
    ( (This)->lpVtbl->remove_OnConnected(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_add_OnDisconnected(This,handler,token) \
    ( (This)->lpVtbl->add_OnDisconnected(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_remove_OnDisconnected(This,token) \
    ( (This)->lpVtbl->remove_OnDisconnected(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_add_OnListening(This,handler,token) \
    ( (This)->lpVtbl->add_OnListening(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_remove_OnListening(This,token) \
    ( (This)->lpVtbl->remove_OnListening(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_BlitRemoteFrame(This,result) \
    ( (This)->lpVtbl->BlitRemoteFrame(This,result) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_CreateDataChannel(This,channelId,priority) \
    ( (This)->lpVtbl->CreateDataChannel(This,channelId,priority) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_add_OnDataChannelCreated(This,handler,token) \
    ( (This)->lpVtbl->add_OnDataChannelCreated(This,handler,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_remove_OnDataChannelCreated(This,token) \
    ( (This)->lpVtbl->remove_OnDataChannelCreated(This,token) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_get_LastFrameStatistics(This,value) \
    ( (This)->lpVtbl->get_LastFrameStatistics(This,value) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext_INTERFACE_DEFINED__) */


/*
 *
 * Interface Microsoft.Holographic.AppRemoting.IPlayerContextStatics
 *
 * Interface is a part of the implementation of type Microsoft.Holographic.AppRemoting.PlayerContext
 *
 *
 */
#if !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__)
#define ____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__
extern const __declspec(selectany) _Null_terminated_ WCHAR InterfaceName_Microsoft_Holographic_AppRemoting_IPlayerContextStatics[] = L"Microsoft.Holographic.AppRemoting.IPlayerContextStatics";
/* [uuid("dfae5c25-4f8a-4980-b670-a6811488cd37"), version, object, exclusiveto] */
typedef struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStaticsVtbl
{
    BEGIN_INTERFACE
    HRESULT ( STDMETHODCALLTYPE *QueryInterface)(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This,
    /* [in] */ __RPC__in REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject
    );

ULONG ( STDMETHODCALLTYPE *AddRef )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This
    );

ULONG ( STDMETHODCALLTYPE *Release )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This
    );

HRESULT ( STDMETHODCALLTYPE *GetIids )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This,
    /* [out] */ __RPC__out ULONG *iidCount,
    /* [size_is][size_is][out] */ __RPC__deref_out_ecount_full_opt(*iidCount) IID **iids
    );

HRESULT ( STDMETHODCALLTYPE *GetRuntimeClassName )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This,
    /* [out] */ __RPC__deref_out_opt HSTRING *className
    );

HRESULT ( STDMETHODCALLTYPE *GetTrustLevel )(
    __RPC__in __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This,
    /* [OUT ] */ __RPC__out TrustLevel *trustLevel
    );
HRESULT ( STDMETHODCALLTYPE *Create )(
        __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics * This,
        /* [out, retval] */__x_Microsoft_CHolographic_CAppRemoting_CIPlayerContext * * result
        );
    END_INTERFACE
    
} __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStaticsVtbl;

interface __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics
{
    CONST_VTBL struct __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStaticsVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_QueryInterface(This,riid,ppvObject) \
( (This)->lpVtbl->QueryInterface(This,riid,ppvObject) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_AddRef(This) \
        ( (This)->lpVtbl->AddRef(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_Release(This) \
        ( (This)->lpVtbl->Release(This) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_GetIids(This,iidCount,iids) \
        ( (This)->lpVtbl->GetIids(This,iidCount,iids) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_GetRuntimeClassName(This,className) \
        ( (This)->lpVtbl->GetRuntimeClassName(This,className) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_GetTrustLevel(This,trustLevel) \
        ( (This)->lpVtbl->GetTrustLevel(This,trustLevel) )

#define __x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_Create(This,result) \
    ( (This)->lpVtbl->Create(This,result) )


#endif /* COBJMACROS */


EXTERN_C const IID IID___x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics;
#endif /* !defined(____x_Microsoft_CHolographic_CAppRemoting_CIPlayerContextStatics_INTERFACE_DEFINED__) */


/*
 *
 * Class Microsoft.Holographic.AppRemoting.PlayerContext
 *
 * RuntimeClass contains static methods.
 *
 * Class implements the following interfaces:
 *    Microsoft.Holographic.AppRemoting.IPlayerContext2
 *    Microsoft.Holographic.AppRemoting.IPlayerContext ** Default Interface **
 *    Windows.Foundation.IClosable
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Microsoft_Holographic_AppRemoting_PlayerContext_DEFINED
#define RUNTIMECLASS_Microsoft_Holographic_AppRemoting_PlayerContext_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Microsoft_Holographic_AppRemoting_PlayerContext[] = L"Microsoft.Holographic.AppRemoting.PlayerContext";
#endif




#endif // defined(__cplusplus)
#pragma pop_macro("MIDL_CONST_ID")
#endif // __Microsoft2EHolographic2EAppRemoting_p_h__

#endif // __Microsoft2EHolographic2EAppRemoting_h__
