#include "Editor/TSDumpWidgetsCommandlet.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Misc/FileHelper.h"
#include "Telescreen.h"
#include "UObject/UnrealType.h"

namespace
{
	FString DescribeFunction(const UFunction* Function)
	{
		FString Params;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			Params += FString::Printf(TEXT("%s%s %s%s"), Params.IsEmpty() ? TEXT("") : TEXT(", "), *It->GetCPPType(), *It->GetName(),
				It->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT(" (return)") : It->HasAnyPropertyFlags(CPF_OutParm) ? TEXT(" (out)") : TEXT(""));
		}
		return FString::Printf(TEXT("%s(%s)"), *Function->GetName(), *Params);
	}

	void DumpTree(UWidget* Widget, int32 Depth, TArray<FString>& Out)
	{
		if (!Widget || Depth > 14)
		{
			return;
		}
		FString Line = FString::ChrN(Depth * 2, TEXT(' ')) + Widget->GetName() + TEXT(" : ") + Widget->GetClass()->GetName();
		if (const INamedSlotInterface* Slots = Cast<INamedSlotInterface>(Widget))
		{
			TArray<FName> Names;
			Slots->GetSlotNames(Names);
			if (Names.Num() > 0)
			{
				FString List;
				for (const FName& Name : Names)
				{
					List += (List.IsEmpty() ? TEXT("") : TEXT(", ")) + Name.ToString();
				}
				Line += TEXT("  slots[") + List + TEXT("]");
			}
		}
		// Layout of canvas children and size box overrides, for placing new widgets beside vanilla ones.
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			const FAnchors Anchors = CanvasSlot->GetAnchors();
			const FMargin Offsets = CanvasSlot->GetOffsets();
			Line += FString::Printf(TEXT("  canvas[anchors %.2f,%.2f-%.2f,%.2f offsets L%.0f T%.0f R%.0f B%.0f align %.2f,%.2f auto %d z %d]"),
				Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y, Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom,
				CanvasSlot->GetAlignment().X, CanvasSlot->GetAlignment().Y, CanvasSlot->GetAutoSize() ? 1 : 0, CanvasSlot->GetZOrder());
		}
		if (const USizeBox* Size = Cast<USizeBox>(Widget))
		{
			Line += FString::Printf(TEXT("  size[w %.0f h %.0f maxw %.0f maxh %.0f]"), Size->GetWidthOverride(), Size->GetHeightOverride(),
				Size->GetMaxDesiredWidth(), Size->GetMaxDesiredHeight());
		}
		Out.Add(Line);
		// Content placed into a child user widget's named slots (e.g. a window's WindowBody).
		if (const UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			TArray<FName> Names;
			User->GetSlotNames(Names);
			for (const FName& Name : Names)
			{
				if (UWidget* Content = User->GetContentForSlot(Name))
				{
					Out.Add(FString::ChrN((Depth + 1) * 2, TEXT(' ')) + TEXT("[slot ") + Name.ToString() + TEXT("]"));
					DumpTree(Content, Depth + 2, Out);
				}
			}
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 I = 0; I < Panel->GetChildrenCount(); ++I)
			{
				DumpTree(Panel->GetChildAt(I), Depth + 1, Out);
			}
		}
	}
}

int32 UTSDumpWidgetsCommandlet::Main(const FString& Params)
{
	FString OutPath, ClassList;
	FParse::Value(*Params, TEXT("out="), OutPath);
	FParse::Value(*Params, TEXT("classes="), ClassList, false);
	TArray<FString> Paths;
	ClassList.ParseIntoArray(Paths, TEXT(","));
	TArray<FString> Out;
	for (const FString& Path : Paths)
	{
		Out.Add(FString::ChrN(100, TEXT('=')));
		Out.Add(Path);
		UClass* Class = LoadClass<UObject>(nullptr, *Path);
		if (!Class)
		{
			Out.Add(TEXT("  NOT FOUND"));
			continue;
		}
		Out.Add(TEXT("parent chain:"));
		for (UClass* Super = Class; Super; Super = Super->GetSuperClass())
		{
			Out.Add(TEXT("  ") + Super->GetPathName());
			if (Super->HasAnyClassFlags(CLASS_Native))
			{
				break;
			}
		}
		Out.Add(TEXT("properties (Blueprint-declared):"));
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetOwnerClass() && It->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
			{
				continue;
			}
			FString Type = It->GetCPPType();
			// Element types of arrays and the members of struct properties, so Blueprint structs can be filled from C++.
			if (const FArrayProperty* Array = CastField<FArrayProperty>(*It))
			{
				Type += TEXT("<") + Array->Inner->GetCPPType() + TEXT(">");
				if (const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner))
				{
					for (TFieldIterator<FProperty> Member(Inner->Struct); Member; ++Member)
					{
						Type += FString::Printf(TEXT(" {%s %s}"), *Member->GetCPPType(), *Member->GetName());
					}
				}
			}
			Out.Add(FString::Printf(TEXT("  %s %s%s"), *Type, *It->GetName(), CastField<FMulticastDelegateProperty>(*It) ? TEXT("  (event dispatcher)") : TEXT("")));
		}
		Out.Add(TEXT("functions (Blueprint-declared):"));
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetOwnerClass() && It->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
			{
				continue;
			}
			if (It->GetName().StartsWith(TEXT("ExecuteUbergraph")) || It->GetName().StartsWith(TEXT("BndEvt")))
			{
				continue;
			}
			Out.Add(TEXT("  ") + DescribeFunction(*It));
		}
		if (const UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(Class))
		{
			Out.Add(TEXT("widget tree:"));
			UWidgetTree* Tree = WidgetClass->GetWidgetTreeArchetype();
			DumpTree(Tree ? Tree->RootWidget : nullptr, 1, Out);
		}
	}
	FFileHelper::SaveStringArrayToFile(Out, *OutPath);
	UE_LOG(LogTelescreen, Display, TEXT("TSDumpWidgets: wrote %d lines to %s"), Out.Num(), *OutPath);
	return 0;
}
