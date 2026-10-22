#include "Tls.h"

#include <winsock2.h>
#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS
#include <subauth.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

namespace
{
	constexpr DWORD RequestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
		ISC_REQ_MANUAL_CRED_VALIDATION;

	std::string Sha256Hex(const uint8_t* Data, size_t Size)
	{
		uint8_t Hash[32] = {};
		if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(Data), ULONG(Size), Hash, sizeof(Hash)) != 0) return std::string();
		static const char* Digits = "0123456789ABCDEF";
		std::string Out;
		for (int I = 0; I < 32; ++I)
		{
			if (I) Out += ':';
			Out += Digits[Hash[I] >> 4];
			Out += Digits[Hash[I] & 15];
		}
		return Out;
	}

	std::string ProtocolName(DWORD Protocol)
	{
		if (Protocol & SP_PROT_TLS1_3_CLIENT) return "TLS 1.3";
		if (Protocol & SP_PROT_TLS1_2_CLIENT) return "TLS 1.2";
		if (Protocol & SP_PROT_TLS1_1_CLIENT) return "TLS 1.1";
		if (Protocol & SP_PROT_TLS1_0_CLIENT) return "TLS 1.0";
		return "TLS";
	}
}

FTlsClient::~FTlsClient()
{
	if (bHaveContext) DeleteSecurityContext(static_cast<CtxtHandle*>(Ctx));
	if (Cred)
	{
		FreeCredentialsHandle(static_cast<CredHandle*>(Cred));
		delete static_cast<CredHandle*>(Cred);
	}
	delete static_cast<CtxtHandle*>(Ctx);
}

bool FTlsClient::SendRaw(const void* Data, size_t Size)
{
	const char* In = static_cast<const char*>(Data);
	while (Size > 0)
	{
		const int Sent = send(SOCKET(Socket), In, int(std::min<size_t>(Size, 1 << 20)), 0);
		if (Sent <= 0) return false;
		In += Sent;
		Size -= size_t(Sent);
	}
	return true;
}

bool FTlsClient::ReadRaw()
{
	uint8_t Buffer[16384];
	const int Got = recv(SOCKET(Socket), reinterpret_cast<char*>(Buffer), sizeof(Buffer), 0);
	if (Got <= 0) return false;
	Incoming.insert(Incoming.end(), Buffer, Buffer + Got);
	return true;
}

bool FTlsClient::Handshake(uintptr_t InSocket, const std::string& Host, std::string& Error)
{
	Socket = InSocket;
	HostName = Host;
	auto* Credentials = new CredHandle();
	// TLS 1.2 and 1.3 with Windows' defaults; the certificate is checked by the caller (pinned).
	SCH_CREDENTIALS Modern{};
	Modern.dwVersion = SCH_CREDENTIALS_VERSION;
	Modern.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
	TimeStamp Expiry;
	SECURITY_STATUS Status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &Modern, nullptr, nullptr,
		Credentials, &Expiry);
	if (Status != SEC_E_OK)
	{
		// Older Windows 10 without SCH_CREDENTIALS.
		SCHANNEL_CRED Legacy{};
		Legacy.dwVersion = SCHANNEL_CRED_VERSION;
		Legacy.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
		Status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &Legacy, nullptr, nullptr, Credentials,
			&Expiry);
	}
	if (Status != SEC_E_OK)
	{
		delete Credentials;
		Error = "Windows' TLS isn't available";
		return false;
	}
	Cred = Credentials;
	Ctx = new CtxtHandle();
	auto* Context = static_cast<CtxtHandle*>(Ctx);

	bool bFirst = true;
	for (int Round = 0; Round < 64; ++Round)
	{
		SecBuffer InBuffers[2] = {{ULONG(Incoming.size()), SECBUFFER_TOKEN, Incoming.data()}, {0, SECBUFFER_EMPTY, nullptr}};
		SecBufferDesc InDesc{SECBUFFER_VERSION, 2, InBuffers};
		SecBuffer OutBuffer{0, SECBUFFER_TOKEN, nullptr};
		SecBufferDesc OutDesc{SECBUFFER_VERSION, 1, &OutBuffer};
		DWORD Flags = 0;
		Status = InitializeSecurityContextA(Credentials, bFirst ? nullptr : Context, bFirst ? const_cast<SEC_CHAR*>(HostName.c_str()) : nullptr, RequestFlags,
			0, 0, bFirst ? nullptr : &InDesc, 0, Context, &OutDesc, &Flags, nullptr);
		bFirst = false;
		bHaveContext = true;
		if (OutBuffer.cbBuffer > 0 && OutBuffer.pvBuffer)
		{
			const bool bSent = SendRaw(OutBuffer.pvBuffer, OutBuffer.cbBuffer);
			FreeContextBuffer(OutBuffer.pvBuffer);
			if (!bSent)
			{
				Error = "the connection closed during the TLS handshake";
				return false;
			}
		}
		if (Status == SEC_E_INCOMPLETE_MESSAGE)
		{
			if (!ReadRaw())
			{
				Error = "the connection closed during the TLS handshake";
				return false;
			}
			continue;
		}
		if (Status != SEC_E_OK && Status != SEC_I_CONTINUE_NEEDED && Status != SEC_I_INCOMPLETE_CREDENTIALS)
		{
			char Text[64];
			snprintf(Text, sizeof(Text), "TLS handshake failed (0x%08lX)", static_cast<unsigned long>(Status));
			Error = Text;
			return false;
		}
		// Keep what the server sent beyond this handshake message.
		if (InBuffers[1].BufferType == SECBUFFER_EXTRA && InBuffers[1].cbBuffer > 0)
		{
			Incoming.erase(Incoming.begin(), Incoming.end() - InBuffers[1].cbBuffer);
		}
		else if (!Incoming.empty() && Round > 0)
		{
			Incoming.clear();
		}
		if (Status == SEC_E_OK) break;
		if (Status == SEC_I_CONTINUE_NEEDED && Incoming.empty() && !ReadRaw())
		{
			Error = "the connection closed during the TLS handshake";
			return false;
		}
	}
	if (Status != SEC_E_OK)
	{
		Error = "the TLS handshake didn't finish";
		return false;
	}
	SecPkgContext_StreamSizes Sizes{};
	QueryContextAttributesW(Context, SECPKG_ATTR_STREAM_SIZES, &Sizes);
	HeaderSize = Sizes.cbHeader;
	TrailerSize = Sizes.cbTrailer;
	MaxMessage = Sizes.cbMaximumMessage;
	PCCERT_CONTEXT Certificate = nullptr;
	if (QueryContextAttributesW(Context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &Certificate) == SEC_E_OK && Certificate)
	{
		Fingerprint = Sha256Hex(Certificate->pbCertEncoded, Certificate->cbCertEncoded);
		CertFreeCertificateContext(Certificate);
	}
	SecPkgContext_ConnectionInfo Info{};
	if (QueryContextAttributesW(Context, SECPKG_ATTR_CONNECTION_INFO, &Info) == SEC_E_OK) Protocol = ProtocolName(Info.dwProtocol);
	if (Fingerprint.empty())
	{
		Error = "the server sent no TLS certificate";
		return false;
	}
	return true;
}

bool FTlsClient::HasBuffered()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return PlainPos < Plain.size();
}

int FTlsClient::Recv(uint8_t* Out, int Max)
{
	auto* Context = static_cast<CtxtHandle*>(Ctx);
	for (;;)
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (PlainPos < Plain.size())
			{
				const int Count = int(std::min<size_t>(size_t(Max), Plain.size() - PlainPos));
				std::memcpy(Out, Plain.data() + PlainPos, size_t(Count));
				PlainPos += size_t(Count);
				if (PlainPos == Plain.size())
				{
					Plain.clear();
					PlainPos = 0;
				}
				return Count;
			}
			if (!Incoming.empty())
			{
				SecBuffer Buffers[4] = {{ULONG(Incoming.size()), SECBUFFER_DATA, Incoming.data()}, {0, SECBUFFER_EMPTY, nullptr},
					{0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}};
				SecBufferDesc Desc{SECBUFFER_VERSION, 4, Buffers};
				const SECURITY_STATUS Status = DecryptMessage(Context, &Desc, 0, nullptr);
				if (Status == SEC_E_OK || Status == SEC_I_RENEGOTIATE)
				{
					std::vector<uint8_t> Extra;
					for (const SecBuffer& Buffer : Buffers)
					{
						if (Buffer.BufferType == SECBUFFER_DATA && Buffer.cbBuffer > 0)
						{
							const uint8_t* Data = static_cast<const uint8_t*>(Buffer.pvBuffer);
							Plain.insert(Plain.end(), Data, Data + Buffer.cbBuffer);
						}
						if (Buffer.BufferType == SECBUFFER_EXTRA && Buffer.cbBuffer > 0)
						{
							const uint8_t* Data = static_cast<const uint8_t*>(Buffer.pvBuffer);
							Extra.assign(Data, Data + Buffer.cbBuffer);
						}
					}
					Incoming = std::move(Extra);
					if (Status == SEC_I_RENEGOTIATE)
					{
						// TLS 1.3 sends post-handshake messages (session tickets); SChannel processes them here.
						SecBuffer InBuffers[2] = {{ULONG(Incoming.size()), SECBUFFER_TOKEN, Incoming.data()}, {0, SECBUFFER_EMPTY, nullptr}};
						SecBufferDesc InDesc{SECBUFFER_VERSION, 2, InBuffers};
						SecBuffer OutBuffer{0, SECBUFFER_TOKEN, nullptr};
						SecBufferDesc OutDesc{SECBUFFER_VERSION, 1, &OutBuffer};
						DWORD Flags = 0;
						InitializeSecurityContextA(static_cast<CredHandle*>(Cred), Context, nullptr, RequestFlags, 0, 0, &InDesc, 0, nullptr, &OutDesc, &Flags,
							nullptr);
						if (OutBuffer.cbBuffer > 0 && OutBuffer.pvBuffer)
						{
							SendRaw(OutBuffer.pvBuffer, OutBuffer.cbBuffer);
							FreeContextBuffer(OutBuffer.pvBuffer);
						}
						if (InBuffers[1].BufferType == SECBUFFER_EXTRA && InBuffers[1].cbBuffer > 0)
						{
							Incoming.erase(Incoming.begin(), Incoming.end() - InBuffers[1].cbBuffer);
						}
						else
						{
							Incoming.clear();
						}
					}
					continue;
				}
				if (Status == SEC_I_CONTEXT_EXPIRED) return 0;
				if (Status != SEC_E_INCOMPLETE_MESSAGE) return -1;
			}
		}
		if (!ReadRaw()) return 0;
	}
}

bool FTlsClient::SendAll(const uint8_t* Data, size_t Size)
{
	auto* Context = static_cast<CtxtHandle*>(Ctx);
	std::vector<uint8_t> Record;
	while (Size > 0)
	{
		const size_t Chunk = std::min<size_t>(Size, MaxMessage ? MaxMessage : 16384);
		Record.resize(HeaderSize + Chunk + TrailerSize);
		std::memcpy(Record.data() + HeaderSize, Data, Chunk);
		SecBuffer Buffers[4] = {{HeaderSize, SECBUFFER_STREAM_HEADER, Record.data()}, {ULONG(Chunk), SECBUFFER_DATA, Record.data() + HeaderSize},
			{TrailerSize, SECBUFFER_STREAM_TRAILER, Record.data() + HeaderSize + Chunk}, {0, SECBUFFER_EMPTY, nullptr}};
		SecBufferDesc Desc{SECBUFFER_VERSION, 4, Buffers};
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (EncryptMessage(Context, 0, &Desc, 0) != SEC_E_OK) return false;
		}
		if (!SendRaw(Record.data(), Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer)) return false;
		Data += Chunk;
		Size -= Chunk;
	}
	return true;
}
