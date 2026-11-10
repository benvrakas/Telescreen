#include "Plex/TSPlex.h"
#include "Buildables/TSBuildableScreen.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Telescreen.h"
#include "TSRemoteCallObject.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace TSPlexImpl
{
	constexpr float ApiTimeout = 15.f;
	// A connection that doesn't answer in this long is skipped for the next one.
	constexpr float ProbeTimeout = 4.f;
	constexpr double PinPollSeconds = 2.0;
	constexpr double PinWaitSeconds = 600.0;
	constexpr double ShareRetrySeconds = 30.0;
	// Temporary tokens last about two days; a new one is made after this.
	constexpr double ShareTokenSeconds = 12.0 * 3600.0;
	constexpr int32 MaxEntries = 500;

	// A failed lookup is tried again after this long.
	constexpr double ResolveRetrySeconds = 60.0;

	struct FResolved
	{
		TSPlex::EResolve State = TSPlex::EResolve::Searching;
		double Time = 0.0;
		FString PlayUrl;
		FString Problem;
	};

	struct FState
	{
		bool bLoaded = false;
		FString ClientId;
		FString AccountToken;
		FString UserName;
		FString LastServer;
		TArray<TSPlex::FServer> Servers;
		bool bServersLoaded = false;
		// The address this PC browses each server at.
		TMap<FString, FString> BaseUrls;
		TMap<FString, TPair<FString, double>> ShareTokens;
		TMap<FString, double> LastShareAttempt;
		TMap<FString, FResolved> Resolved;
		uint32 Generation = 1;
		bool bSigningIn = false;
		int32 SignInId = 0;
		FTSTicker::FDelegateHandle PinTicker;
	};

	FState& State()
	{
		static FState Instance;
		return Instance;
	}

	// Quiet field readers: FJsonObject's getters log a warning for every field that's absent.
	FString Str(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		FString Value;
		if (Object.IsValid() && !Object->TryGetStringField(Name, Value))
		{
			double Number = 0.0;
			if (Object->TryGetNumberField(Name, Number))
			{
				Value = FString::Printf(TEXT("%lld"), int64(Number));
			}
		}
		return Value;
	}

	double Num(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		double Value = 0.0;
		return Object.IsValid() && Object->TryGetNumberField(Name, Value) ? Value : 0.0;
	}

	bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		bool Value = false;
		return Object.IsValid() && Object->TryGetBoolField(Name, Value) && Value;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Value.IsValid() && Value->TryGetObject(Object) && Object ? *Object : nullptr;
	}

	FString SettingsPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("Plex.json");
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
		if (!CryptProtectData(&In, L"Telescreen Plex", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Plex: could not encrypt the sign-in (error %u); it won't be kept"), GetLastError());
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
			UE_LOG(LogTelescreen, Warning, TEXT("Plex: the saved sign-in couldn't be decrypted (another Windows user?); sign in again"));
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

	void Save()
	{
		FState& S = State();
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("client"), S.ClientId);
		Root->SetStringField(TEXT("token"), ProtectText(S.AccountToken));
		Root->SetStringField(TEXT("user"), S.UserName);
		Root->SetStringField(TEXT("server"), S.LastServer);
		FString Json;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
		if (!FFileHelper::SaveStringToFile(Json, *SettingsPath()))
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Plex: could not save %s"), *SettingsPath());
		}
	}

	FState& Loaded()
	{
		FState& S = State();
		if (S.bLoaded)
		{
			return S;
		}
		S.bLoaded = true;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Json, *SettingsPath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())
		{
			S.ClientId = Str(Root, TEXT("client"));
			S.AccountToken = UnprotectText(Str(Root, TEXT("token")));
			S.UserName = Str(Root, TEXT("user"));
			S.LastServer = Str(Root, TEXT("server"));
		}
		if (S.ClientId.IsEmpty())
		{
			S.ClientId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
			Save();
		}
		UE_LOG(LogTelescreen, Log, TEXT("Plex: %s"), S.AccountToken.IsEmpty() ? TEXT("not signed in") : *FString::Printf(TEXT("signed in as %s"), *S.UserName));
		return S;
	}

	FString ModVersion()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Telescreen"));
		return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString(TEXT("1"));
	}

	// The URL without query parameters, for logs (no tokens).
	FString ForLog(const FString& Url)
	{
		FString Left;
		return Url.Split(TEXT("?"), &Left, nullptr) ? Left : Url;
	}

	void Request(const FString& Verb, const FString& Url, const FString& Token, float Timeout, TFunction<void(int32 Code, const FString& Body)> OnDone)
	{
		FState& S = Loaded();
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
		Http->SetVerb(Verb);
		Http->SetURL(Url);
		Http->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Http->SetHeader(TEXT("X-Plex-Product"), TEXT("Telescreen"));
		Http->SetHeader(TEXT("X-Plex-Version"), ModVersion());
		Http->SetHeader(TEXT("X-Plex-Client-Identifier"), S.ClientId);
		Http->SetHeader(TEXT("X-Plex-Platform"), TEXT("Windows"));
		Http->SetHeader(TEXT("X-Plex-Device-Name"), TEXT("Satisfactory (Telescreen)"));
		if (!Token.IsEmpty())
		{
			Http->SetHeader(TEXT("X-Plex-Token"), Token);
		}
		Http->SetTimeout(Timeout);
		const FString Logged = ForLog(Url);
		Http->OnProcessRequestComplete().BindLambda([OnDone = MoveTemp(OnDone), Logged](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			UE_LOG(LogTelescreen, Verbose, TEXT("Plex: %s -> %d"), *Logged, Code);
			OnDone(Code, Response.IsValid() ? Response->GetContentAsString() : FString());
		});
		Http->ProcessRequest();
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Body)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root);
		return Root;
	}

	FString CodeProblem(int32 Code)
	{
		return Code == 0 ? FString(TEXT("no answer")) : Code == 401 ? FString(TEXT("not allowed (sign in again)")) : FString::Printf(TEXT("error %d"), Code);
	}

	FString FormatDuration(float Seconds)
	{
		const int32 Total = FMath::RoundToInt(Seconds);
		return Total >= 3600 ? FString::Printf(TEXT("%d:%02d:%02d"), Total / 3600, Total / 60 % 60, Total % 60) : FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	// An entry from a Plex metadata object; false for kinds that can't be shown on a screen.
	bool ReadEntry(const TSharedPtr<FJsonObject>& Item, TSPlex::FEntry& Out)
	{
		if (!Item.IsValid())
		{
			return false;
		}
		Out.Type = Str(Item, TEXT("type"));
		if (Out.Type != TEXT("movie") && Out.Type != TEXT("show") && Out.Type != TEXT("season") && Out.Type != TEXT("episode") && Out.Type != TEXT("clip"))
		{
			return false;
		}
		Out.Title = Str(Item, TEXT("title"));
		Out.Key = Str(Item, TEXT("key"));
		Out.RatingKey = Str(Item, TEXT("ratingKey"));
		Out.Duration = float(Num(Item, TEXT("duration")) / 1000.0);
		const int32 Year = int32(Num(Item, TEXT("year")));
		const FString Length = Out.Duration > 0.f ? FormatDuration(Out.Duration) : FString();
		if (Out.Type == TEXT("episode"))
		{
			const int32 Season = int32(Num(Item, TEXT("parentIndex")));
			const int32 Episode = int32(Num(Item, TEXT("index")));
			const FString Show = Str(Item, TEXT("grandparentTitle"));
			const FString Number = FString::Printf(TEXT("S%02dE%02d"), Season, Episode);
			Out.Details = FString::Join(TArray<FString>{Show, Number, Length}.FilterByPredicate([](const FString& Part) { return !Part.IsEmpty(); }), TEXT("   "));
			Out.QueueTitle = Show.IsEmpty() ? Out.Title : FString::Printf(TEXT("%s %s - %s"), *Show, *Number, *Out.Title);
		}
		else if (Out.Type == TEXT("season"))
		{
			Out.Details = Str(Item, TEXT("parentTitle"));
		}
		else if (Out.Type == TEXT("show"))
		{
			const int32 Seasons = int32(Num(Item, TEXT("childCount")));
			Out.Details = Seasons > 0 ? FString::Printf(TEXT("Show, %d season%s"), Seasons, Seasons == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("Show"));
		}
		else
		{
			Out.Details = FString::Join(TArray<FString>{Year > 0 ? FString::FromInt(Year) : FString(), Length}.FilterByPredicate([](const FString& Part) { return !Part.IsEmpty(); }), TEXT("   "));
			Out.QueueTitle = Year > 0 ? FString::Printf(TEXT("%s (%d)"), *Out.Title, Year) : Out.Title;
		}
		if (Out.QueueTitle.IsEmpty())
		{
			Out.QueueTitle = Out.Title;
		}
		return true;
	}

	TSharedPtr<FJsonObject> Container(const FString& Body)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(Body);
		const TSharedPtr<FJsonObject>* Media = nullptr;
		return Root.IsValid() && Root->TryGetObjectField(TEXT("MediaContainer"), Media) && Media ? *Media : nullptr;
	}

	// Calls OnDone with the first of Server's connections that answers, trying them in order.
	void FindBase(const TSPlex::FServer& Server, TFunction<void(const FString& Base)> OnDone)
	{
		FState& S = Loaded();
		if (const FString* Known = S.BaseUrls.Find(Server.MachineId))
		{
			OnDone(*Known);
			return;
		}
		TSharedRef<int32> Index = MakeShared<int32>(0);
		TSharedRef<TFunction<void()>> TryNext = MakeShared<TFunction<void()>>();
		// The pending request holds the chain; the chain only points back at itself weakly.
		TWeakPtr<TFunction<void()>> WeakTry = TryNext;
		*TryNext = [Server, Index, WeakTry, OnDone = MoveTemp(OnDone)]()
		{
			TSharedPtr<TFunction<void()>> TryNext = WeakTry.Pin();
			if (*Index >= Server.Connections.Num())
			{
				UE_LOG(LogTelescreen, Warning, TEXT("Plex: none of %s's %d address(es) answered"), *Server.Name, Server.Connections.Num());
				OnDone(FString());
				return;
			}
			const FString Base = Server.Connections[(*Index)++];
			Request(TEXT("GET"), Base + TEXT("/identity"), Server.AccessToken, ProbeTimeout, [Server, Base, TryNext, OnDone](int32 Code, const FString&)
			{
				if (Code == 200)
				{
					UE_LOG(LogTelescreen, Log, TEXT("Plex: browsing %s at %s"), *Server.Name, *Base);
					State().BaseUrls.Add(Server.MachineId, Base);
					OnDone(Base);
					return;
				}
				(*TryNext)();
			});
		};
		(*TryNext)();
	}

	void Bump()
	{
		++State().Generation;
	}
}

namespace TSPlex
{
using namespace TSPlexImpl;

bool IsPlexUrl(const FString& Url)
{
	return Url.StartsWith(TEXT("plex://"), ESearchCase::IgnoreCase);
}

FString MakeUrl(const FString& MachineId, const FString& RatingKey)
{
	return FString::Printf(TEXT("plex://%s/%s"), *MachineId, *RatingKey);
}

bool ParseUrl(const FString& Url, FString& OutMachineId, FString& OutRatingKey)
{
	if (!IsPlexUrl(Url))
	{
		return false;
	}
	return Url.Mid(7).Split(TEXT("/"), &OutMachineId, &OutRatingKey) && !OutMachineId.IsEmpty() && !OutRatingKey.IsEmpty() && OutRatingKey.IsNumeric();
}

bool IsSignedIn()
{
	return !Loaded().AccountToken.IsEmpty();
}

FString GetUserName()
{
	return Loaded().UserName;
}

bool IsSigningIn()
{
	return State().bSigningIn;
}

void CancelSignIn()
{
	FState& S = State();
	if (S.PinTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(S.PinTicker);
		S.PinTicker.Reset();
	}
	S.bSigningIn = false;
	++S.SignInId;
}

void SignIn(TFunction<void(const FString& Code)> OnCode, TFunction<void(bool bOk, const FString& Error)> OnDone)
{
	CancelSignIn();
	FState& S = Loaded();
	S.bSigningIn = true;
	const int32 Id = S.SignInId;
	UE_LOG(LogTelescreen, Log, TEXT("Plex: asking plex.tv for a link code"));
	Request(TEXT("POST"), TEXT("https://plex.tv/api/v2/pins"), FString(), ApiTimeout, [Id, OnCode = MoveTemp(OnCode), OnDone](int32 Code, const FString& Body)
	{
		if (State().SignInId != Id)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Pin = ParseObject(Body);
		if ((Code != 200 && Code != 201) || !Pin.IsValid() || Str(Pin, TEXT("code")).IsEmpty())
		{
			State().bSigningIn = false;
			OnDone(false, FString::Printf(TEXT("plex.tv didn't give a link code (%s)"), *CodeProblem(Code)));
			return;
		}
		const FString PinId = FString::Printf(TEXT("%lld"), int64(Num(Pin, TEXT("id"))));
		OnCode(Str(Pin, TEXT("code")));
		const double Until = FPlatformTime::Seconds() + PinWaitSeconds;
		TSharedRef<bool> bWaiting = MakeShared<bool>(false);
		State().PinTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Id, PinId, Until, bWaiting, OnDone](float)
		{
			FState& S = State();
			if (S.SignInId != Id)
			{
				return false;
			}
			if (FPlatformTime::Seconds() > Until)
			{
				CancelSignIn();
				OnDone(false, TEXT("The link code expired; press Sign in again."));
				return false;
			}
			if (*bWaiting)
			{
				return true;
			}
			*bWaiting = true;
			Request(TEXT("GET"), TEXT("https://plex.tv/api/v2/pins/") + PinId, FString(), ApiTimeout, [Id, bWaiting, OnDone](int32 Code, const FString& Body)
			{
				*bWaiting = false;
				FState& S = State();
				const TSharedPtr<FJsonObject> Pin = ParseObject(Body);
				FString Token;
				if (S.SignInId != Id || Code != 200 || !Pin.IsValid() || !Pin->TryGetStringField(TEXT("authToken"), Token) || Token.IsEmpty())
				{
					return;
				}
				CancelSignIn();
				S.AccountToken = Token;
				S.Servers.Reset();
				S.bServersLoaded = false;
				S.BaseUrls.Reset();
				S.ShareTokens.Reset();
				Request(TEXT("GET"), TEXT("https://plex.tv/api/v2/user"), Token, ApiTimeout, [OnDone](int32 Code, const FString& Body)
				{
					FState& S = State();
					const TSharedPtr<FJsonObject> User = ParseObject(Body);
					S.UserName = User.IsValid() ? Str(User, TEXT("username")) : FString();
					if (S.UserName.IsEmpty() && User.IsValid())
					{
						S.UserName = Str(User, TEXT("title"));
					}
					Save();
					UE_LOG(LogTelescreen, Log, TEXT("Plex: signed in as %s"), *S.UserName);
					Bump();
					OnDone(true, FString());
				});
			});
			return true;
		}), float(PinPollSeconds));
	});
}

void SignOut()
{
	CancelSignIn();
	FState& S = Loaded();
	UE_LOG(LogTelescreen, Log, TEXT("Plex: signed out"));
	S.AccountToken.Reset();
	S.UserName.Reset();
	S.Servers.Reset();
	S.bServersLoaded = false;
	S.BaseUrls.Reset();
	S.ShareTokens.Reset();
	Save();
	Bump();
}

void ListServers(bool bRefresh, TFunction<void(bool bOk, const TArray<FServer>& Servers, const FString& Error)> OnDone)
{
	FState& S = Loaded();
	if (S.AccountToken.IsEmpty())
	{
		OnDone(false, {}, TEXT("Not signed in to Plex."));
		return;
	}
	if (S.bServersLoaded && !bRefresh)
	{
		OnDone(true, S.Servers, FString());
		return;
	}
	Request(TEXT("GET"), TEXT("https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1"), S.AccountToken, ApiTimeout, [OnDone = MoveTemp(OnDone)](int32 Code, const FString& Body)
	{
		FState& S = State();
		TArray<TSharedPtr<FJsonValue>> Resources;
		if (Code != 200 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Resources))
		{
			if (Code == 401)
			{
				SignOut();
			}
			OnDone(false, {}, FString::Printf(TEXT("plex.tv didn't list your servers (%s)"), *CodeProblem(Code)));
			return;
		}
		S.Servers.Reset();
		for (const TSharedPtr<FJsonValue>& Value : Resources)
		{
			const TSharedPtr<FJsonObject> Resource = Obj(Value);
			if (!Resource.IsValid() || !Str(Resource, TEXT("provides")).Contains(TEXT("server")))
			{
				continue;
			}
			FServer Server;
			Server.Name = Str(Resource, TEXT("name"));
			Server.MachineId = Str(Resource, TEXT("clientIdentifier"));
			Server.bOwned = Flag(Resource, TEXT("owned"));
			Server.AccessToken = Str(Resource, TEXT("accessToken"));
			// Local addresses first (fast at home), then remote ones, then Plex's relay.
			TArray<FString> Local, Remote, Relay;
			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (Resource->TryGetArrayField(TEXT("connections"), Connections))
			{
				for (const TSharedPtr<FJsonValue>& ConnectionValue : *Connections)
				{
					const TSharedPtr<FJsonObject> Connection = Obj(ConnectionValue);
					const FString Uri = Connection.IsValid() ? Str(Connection, TEXT("uri")) : FString();
					if (Uri.IsEmpty())
					{
						continue;
					}
					(Flag(Connection, TEXT("relay")) ? Relay : Flag(Connection, TEXT("local")) ? Local : Remote).AddUnique(Uri);
				}
			}
			Server.Connections = Local;
			Server.Connections.Append(Remote);
			Server.Connections.Append(Relay);
			Server.ShareConnections = Remote;
			Server.ShareConnections.Append(Local);
			Server.ShareConnections.Append(Relay);
			if (!Server.MachineId.IsEmpty() && Server.Connections.Num() > 0)
			{
				S.Servers.Add(MoveTemp(Server));
			}
		}
		S.bServersLoaded = true;
		UE_LOG(LogTelescreen, Log, TEXT("Plex: %d server(s)"), S.Servers.Num());
		OnDone(true, S.Servers, FString());
	});
}

const FServer* FindServer(const FString& MachineId)
{
	return Loaded().Servers.FindByPredicate([&MachineId](const FServer& Server) { return Server.MachineId == MachineId; });
}

FString GetLastServer()
{
	return Loaded().LastServer;
}

void SetLastServer(const FString& MachineId)
{
	FState& S = Loaded();
	if (S.LastServer != MachineId)
	{
		S.LastServer = MachineId;
		Save();
	}
}

void Browse(const FServer& Server, const FString& Path, TFunction<void(bool bOk, const TArray<FEntry>& Entries, const FString& Error)> OnDone)
{
	FindBase(Server, [Server, Path, OnDone = MoveTemp(OnDone)](const FString& Base)
	{
		if (Base.IsEmpty())
		{
			OnDone(false, {}, FString::Printf(TEXT("Can't reach %s from this PC."), *Server.Name));
			return;
		}
		const bool bSections = Path.IsEmpty();
		const FString Url = Base + (bSections ? FString(TEXT("/library/sections")) : Path) +
			(bSections ? FString() : FString::Printf(TEXT("%sX-Plex-Container-Start=0&X-Plex-Container-Size=%d"), Path.Contains(TEXT("?")) ? TEXT("&") : TEXT("?"), MaxEntries));
		Request(TEXT("GET"), Url, Server.AccessToken, ApiTimeout, [bSections, Server, OnDone](int32 Code, const FString& Body)
		{
			const TSharedPtr<FJsonObject> Media = Container(Body);
			if (Code != 200 || !Media.IsValid())
			{
				if (Code == 0)
				{
					State().BaseUrls.Remove(Server.MachineId);
				}
				OnDone(false, {}, FString::Printf(TEXT("%s didn't answer (%s)"), *Server.Name, *CodeProblem(Code)));
				return;
			}
			TArray<FEntry> Entries;
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (bSections)
			{
				if (Media->TryGetArrayField(TEXT("Directory"), Items))
				{
					for (const TSharedPtr<FJsonValue>& Value : *Items)
					{
						const TSharedPtr<FJsonObject> Section = Obj(Value);
						const FString Kind = Section.IsValid() ? Str(Section, TEXT("type")) : FString();
						if (Kind != TEXT("movie") && Kind != TEXT("show"))
						{
							continue;
						}
						FEntry Entry;
						Entry.Type = TEXT("section");
						Entry.Title = Str(Section, TEXT("title"));
						Entry.Details = Kind == TEXT("movie") ? TEXT("Movies") : TEXT("TV shows");
						Entry.Key = FString::Printf(TEXT("/library/sections/%s/all"), *Str(Section, TEXT("key")));
						Entries.Add(MoveTemp(Entry));
					}
				}
			}
			else if (Media->TryGetArrayField(TEXT("Metadata"), Items))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Items)
				{
					FEntry Entry;
					if (ReadEntry(Obj(Value), Entry))
					{
						Entries.Add(MoveTemp(Entry));
					}
				}
			}
			OnDone(true, Entries, FString());
		});
	});
}

void Search(const FServer& Server, const FString& Query, TFunction<void(bool bOk, const TArray<FEntry>& Entries, const FString& Error)> OnDone)
{
	FindBase(Server, [Server, Query, OnDone = MoveTemp(OnDone)](const FString& Base)
	{
		if (Base.IsEmpty())
		{
			OnDone(false, {}, FString::Printf(TEXT("Can't reach %s from this PC."), *Server.Name));
			return;
		}
		const FString Url = FString::Printf(TEXT("%s/hubs/search?query=%s&limit=30"), *Base, *FGenericPlatformHttp::UrlEncode(Query));
		Request(TEXT("GET"), Url, Server.AccessToken, ApiTimeout, [Server, OnDone](int32 Code, const FString& Body)
		{
			const TSharedPtr<FJsonObject> Media = Container(Body);
			if (Code != 200 || !Media.IsValid())
			{
				OnDone(false, {}, FString::Printf(TEXT("%s didn't answer the search (%s)"), *Server.Name, *CodeProblem(Code)));
				return;
			}
			TArray<FEntry> Entries;
			const TArray<TSharedPtr<FJsonValue>>* Hubs = nullptr;
			if (Media->TryGetArrayField(TEXT("Hub"), Hubs))
			{
				for (const TSharedPtr<FJsonValue>& HubValue : *Hubs)
				{
					const TSharedPtr<FJsonObject> Hub = Obj(HubValue);
					const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
					if (!Hub.IsValid() || !Hub->TryGetArrayField(TEXT("Metadata"), Items))
					{
						continue;
					}
					for (const TSharedPtr<FJsonValue>& Value : *Items)
					{
						FEntry Entry;
						if (ReadEntry(Obj(Value), Entry))
						{
							Entries.Add(MoveTemp(Entry));
						}
					}
				}
			}
			OnDone(true, Entries, FString());
		});
	});
}

void MakeShare(const FServer& Server, TFunction<void(bool bOk, const FTSPlexShare& Share, const FString& Error)> OnDone)
{
	FTSPlexShare Share;
	Share.MachineId = Server.MachineId;
	Share.ServerName = Server.Name;
	// Remote addresses first for the other players, then local ones (players at the server's home), then the relay.
	Share.Connections = Server.ShareConnections;
	FState& S = Loaded();
	if (const TPair<FString, double>* Cached = S.ShareTokens.Find(Server.MachineId); Cached && FPlatformTime::Seconds() - Cached->Value < ShareTokenSeconds)
	{
		Share.Token = Cached->Key;
		OnDone(true, Share, FString());
		return;
	}
	FindBase(Server, [Server, Share, OnDone = MoveTemp(OnDone)](const FString& Base) mutable
	{
		if (Base.IsEmpty())
		{
			OnDone(false, Share, FString::Printf(TEXT("Can't reach %s from this PC."), *Server.Name));
			return;
		}
		Request(TEXT("GET"), Base + TEXT("/security/token?type=delegation&scope=all"), Server.AccessToken, ApiTimeout,
			[Server, Share, OnDone](int32 Code, const FString& Body) mutable
		{
			const TSharedPtr<FJsonObject> Media = Container(Body);
			FString Token = Media.IsValid() ? Str(Media, TEXT("token")) : FString();
			if (Token.IsEmpty() && !Server.bOwned)
			{
				// A server shared with this account: its access token only works on that server.
				Token = Server.AccessToken;
			}
			if (Token.IsEmpty())
			{
				UE_LOG(LogTelescreen, Warning, TEXT("Plex: %s gave no temporary token (%s)"), *Server.Name, *CodeProblem(Code));
				OnDone(false, Share, FString::Printf(TEXT("%s didn't give a token to share (%s)"), *Server.Name, *CodeProblem(Code)));
				return;
			}
			State().ShareTokens.Add(Server.MachineId, TPair<FString, double>(Token, FPlatformTime::Seconds()));
			Share.Token = Token;
			UE_LOG(LogTelescreen, Log, TEXT("Plex: made a share for %s (%d address(es))"), *Server.Name, Share.Connections.Num());
			OnDone(true, Share, FString());
		});
	});
}

void EnsureShared(ATSBuildableScreen* Screen, const FString& MachineId)
{
	FState& S = Loaded();
	if (!Screen || S.AccountToken.IsEmpty())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (const double* Last = S.LastShareAttempt.Find(MachineId); Last && Now - *Last < ShareRetrySeconds)
	{
		return;
	}
	S.LastShareAttempt.Add(MachineId, Now);
	TWeakObjectPtr<ATSBuildableScreen> WeakScreen(Screen);
	ListServers(false, [WeakScreen, MachineId](bool bOk, const TArray<FServer>&, const FString&)
	{
		const FServer* Server = bOk ? FindServer(MachineId) : nullptr;
		if (!Server || !WeakScreen.IsValid())
		{
			return;
		}
		// A share that failed for this player has a token that no longer works; make a new one.
		State().ShareTokens.Remove(MachineId);
		MakeShare(*Server, [WeakScreen](bool bShareOk, const FTSPlexShare& Share, const FString& Error)
		{
			UTSRemoteCallObject* RCO = WeakScreen.IsValid() ? UTSRemoteCallObject::GetLocal(WeakScreen.Get()) : nullptr;
			if (bShareOk && RCO)
			{
				UE_LOG(LogTelescreen, Log, TEXT("Plex: sharing %s on %s"), *Share.ServerName, *WeakScreen->GetName());
				RCO->Server_SharePlex(WeakScreen.Get(), Share);
			}
		});
	});
}

EResolve Resolve(const ATSBuildableScreen* Screen, const FString& Url, FString& OutPlayUrl, FString& OutProblem)
{
	FString MachineId, RatingKey;
	if (!Screen || !ParseUrl(Url, MachineId, RatingKey))
	{
		OutProblem = TEXT("not a Plex link");
		return EResolve::Missing;
	}
	const FTSPlexShare* Share = Screen->FindPlexShare(MachineId);
	if (!Share || Share->Token.IsEmpty() || Share->Connections.Num() == 0)
	{
		OutProblem = TEXT("its Plex server isn't shared on this screen yet");
		return EResolve::Missing;
	}
	FState& S = Loaded();
	const FString Key = MachineId + TEXT("|") + RatingKey + TEXT("|") + Share->Token;
	if (const FResolved* Failed = S.Resolved.Find(Key); Failed && Failed->State == EResolve::Missing && FPlatformTime::Seconds() - Failed->Time > ResolveRetrySeconds)
	{
		S.Resolved.Remove(Key);
	}
	if (const FResolved* Known = S.Resolved.Find(Key))
	{
		OutPlayUrl = Known->PlayUrl;
		OutProblem = Known->Problem;
		return Known->State;
	}
	S.Resolved.Add(Key);
	UE_LOG(LogTelescreen, Log, TEXT("Plex: looking up item %s on %s (%d address(es))"), *RatingKey, *Share->ServerName, Share->Connections.Num());
	const TArray<FString> Connections = Share->Connections;
	const FString Token = Share->Token;
	const FString ServerName = Share->ServerName;
	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<FString> Problems = MakeShared<FString>();
	TSharedRef<TFunction<void()>> TryNext = MakeShared<TFunction<void()>>();
	TWeakPtr<TFunction<void()>> WeakTry = TryNext;
	*TryNext = [Key, Connections, Token, RatingKey, ServerName, Index, Problems, WeakTry]()
	{
		TSharedPtr<TFunction<void()>> TryNext = WeakTry.Pin();
		if (*Index >= Connections.Num())
		{
			FResolved& Result = State().Resolved.FindOrAdd(Key);
			Result.State = EResolve::Missing;
			Result.Time = FPlatformTime::Seconds();
			Result.Problem = FString::Printf(TEXT("can't reach %s's Plex server from this PC (%s)"), *ServerName, **Problems);
			UE_LOG(LogTelescreen, Warning, TEXT("Plex: item %s: %s"), *RatingKey, *Result.Problem);
			Bump();
			return;
		}
		const FString Base = Connections[(*Index)++];
		Request(TEXT("GET"), Base + TEXT("/library/metadata/") + RatingKey, Token, ProbeTimeout + 2.f, [Key, Base, Token, RatingKey, Problems, TryNext](int32 Code, const FString& Body)
		{
			const TSharedPtr<FJsonObject> Media = Container(Body);
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			FString PartKey;
			if (Code == 200 && Media.IsValid() && Media->TryGetArrayField(TEXT("Metadata"), Items) && Items->Num() > 0)
			{
				const TSharedPtr<FJsonObject> Item = Obj((*Items)[0]);
				const TArray<TSharedPtr<FJsonValue>>* MediaList = nullptr;
				if (Item.IsValid() && Item->TryGetArrayField(TEXT("Media"), MediaList) && MediaList->Num() > 0)
				{
					const TSharedPtr<FJsonObject> First = Obj((*MediaList)[0]);
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if (First.IsValid() && First->TryGetArrayField(TEXT("Part"), Parts) && Parts->Num() > 0 && Obj((*Parts)[0]).IsValid())
					{
						PartKey = Str(Obj((*Parts)[0]), TEXT("key"));
					}
				}
			}
			if (PartKey.IsEmpty())
			{
				Problems->Append(FString::Printf(TEXT("%s%s"), Problems->IsEmpty() ? TEXT("") : TEXT(", "), *CodeProblem(Code == 200 ? 404 : Code)));
				(*TryNext)();
				return;
			}
			FResolved& Result = State().Resolved.FindOrAdd(Key);
			Result.State = EResolve::Found;
			Result.PlayUrl = Base + PartKey + (PartKey.Contains(TEXT("?")) ? TEXT("&") : TEXT("?")) + TEXT("X-Plex-Token=") + Token;
			UE_LOG(LogTelescreen, Log, TEXT("Plex: item %s plays from %s"), *RatingKey, *ForLog(Result.PlayUrl));
			Bump();
		});
	};
	(*TryNext)();
	OutProblem = TEXT("looking it up on the Plex server");
	return EResolve::Searching;
}

uint32 GetGeneration()
{
	return State().Generation;
}
}
