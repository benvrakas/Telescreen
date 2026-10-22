#include "Screen/TSRemoteConnections.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Telescreen.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	TMap<FString, FTSRemoteConnection> PendingConnections;

	FString SavedConnectionsPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("RemoteConnections.json");
	}

	// Encrypts Text for the current Windows user (DPAPI) and returns it as Base64; empty stays empty.
	FString ProtectText(const FString& Text)
	{
#if PLATFORM_WINDOWS
		if (Text.IsEmpty())
		{
			return FString();
		}
		const FTCHARToUTF8 Utf8(*Text);
		DATA_BLOB In{DWORD(Utf8.Length()), reinterpret_cast<BYTE*>(const_cast<char*>(Utf8.Get()))};
		DATA_BLOB Out{};
		if (!CryptProtectData(&In, L"Telescreen", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Remote connections: could not encrypt a password (error %u); not saved"), GetLastError());
			return FString();
		}
		const FString Encoded = FBase64::Encode(Out.pbData, Out.cbData);
		LocalFree(Out.pbData);
		return Encoded;
#else
		return FString();
#endif
	}

	FString UnprotectText(const FString& Encoded)
	{
#if PLATFORM_WINDOWS
		TArray<uint8> Bytes;
		if (Encoded.IsEmpty() || !FBase64::Decode(Encoded, Bytes))
		{
			return FString();
		}
		DATA_BLOB In{DWORD(Bytes.Num()), Bytes.GetData()};
		DATA_BLOB Out{};
		if (!CryptUnprotectData(&In, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Remote connections: a saved password couldn't be decrypted (another Windows user?)"));
			return FString();
		}
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Out.pbData), Out.cbData);
		FString Result(Text.Length(), Text.Get());
		SecureZeroMemory(Out.pbData, Out.cbData);
		LocalFree(Out.pbData);
		return Result;
#else
		return FString();
#endif
	}
}

bool TSRemoteConnections::Parse(const FString& Address, const FString& Password, FTSRemoteConnection& Out)
{
	FString Text = Address.TrimStartAndEnd();
	Text.RemoveFromStart(TEXT("vnc://"));
	FString Host = Text, PortText;
	// The last colon separates the port, unless the address is a bare IPv6 one.
	int32 Colon = INDEX_NONE;
	if (Text.FindLastChar(TEXT(':'), Colon) && Text.Find(TEXT(":")) == Colon)
	{
		Host = Text.Left(Colon);
		PortText = Text.Mid(Colon + 1);
	}
	Host.RemoveFromStart(TEXT("["));
	Host.RemoveFromEnd(TEXT("]"));
	if (Host.IsEmpty())
	{
		return false;
	}
	int32 Port = 5900;
	if (!PortText.IsEmpty())
	{
		Port = FCString::Atoi(*PortText);
		if (Port >= 0 && Port < 100)
		{
			Port += 5900;
		}
	}
	Out.Host = Host;
	Out.Port = uint16(FMath::Clamp(Port, 1, 65535));
	Out.Password = Password;
	return true;
}

FString TSRemoteConnections::MakePending(const FTSRemoteConnection& Connection)
{
	const FString Token = TEXT("rd:") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(16);
	PendingConnections.Add(Token, Connection);
	return Token;
}

const FTSRemoteConnection* TSRemoteConnections::FindPending(const FString& Token)
{
	return PendingConnections.Find(Token);
}

TArray<FTSRemoteConnection> TSRemoteConnections::LoadSaved()
{
	TArray<FTSRemoteConnection> Connections;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *SavedConnectionsPath()))
	{
		return Connections;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Remote connections: %s is not valid JSON"), *SavedConnectionsPath());
		return Connections;
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("connections"), Items))
	{
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Item->TryGetObject(Object))
			{
				FTSRemoteConnection& Connection = Connections.AddDefaulted_GetRef();
				Connection.Host = (*Object)->GetStringField(TEXT("host"));
				Connection.Port = uint16(FMath::Clamp(int32((*Object)->GetNumberField(TEXT("port"))), 1, 65535));
				Connection.Password = UnprotectText((*Object)->GetStringField(TEXT("password")));
			}
		}
	}
	return Connections;
}

void TSRemoteConnections::Save(const TArray<FTSRemoteConnection>& Connections)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FTSRemoteConnection& Connection : Connections)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("host"), Connection.Host);
		Object->SetNumberField(TEXT("port"), Connection.Port);
		Object->SetStringField(TEXT("password"), ProtectText(Connection.Password));
		Items.Add(MakeShared<FJsonValueObject>(Object));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("connections"), Items);
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	if (!FFileHelper::SaveStringToFile(Json, *SavedConnectionsPath()))
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Remote connections: could not write %s"), *SavedConnectionsPath());
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("Remote connections: %d saved"), Connections.Num());
}
