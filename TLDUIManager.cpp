// TLDUIManager.cpp — Centralized overlays + GAS + Channel, DesignerBase event bus

#include "TLDUIManager.h"

     // current config asset (Overlays map)
#include "TLDUIShared.h"                 // ETLDUIOverlay, FTLDOverlayConfig, input modes, transitions
#include "TLDDesignerWidgetBase.h"       // unified base (ApplyUIEvent)
#include "TLDUIEvents.h"                 // payload UObjects
#include "TLDCombatUIData.h"             // legacy bag (optional)

#include "TLDChannelComponent.h"         // optional channel component
#include "TLDAttributeSet_Combat.h"      // Health attributes

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "TLDProjectSettings.h"

#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "TLDUIEvents.h"
#include "GameFramework/PlayerController.h"

#include "TLDGameState.h"
#include "TLDGameplayTags.h"
#include "TLDAttributeSet_Combat.h"

#include "TLDMainHUD.h"


/* ========================================================================== */
/*  Subsystem lifecycle                                                       */
/* ========================================================================== */

void UTLDUIManager::LoadConfigFromProjectSettings()
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] LoadConfigFromProjectSettings called"));
	
	const UTLDProjectSettings* Settings = UTLDProjectSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("[UIManager] Failed to get TLD Project Settings"));
		return;
	}

	if (Settings->UIConfigAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UIManager] UI Config Asset not set in Project Settings"));
		UE_LOG(LogTemp, Warning, TEXT("[UIManager] Go to Edit->Project Settings->TLD Project Settings to set the UI Config Asset"));
		return;
	}

	// Load the config asset synchronously during subsystem initialization
	UTLDUiConfig* LoadedConfig = Settings->UIConfigAsset.LoadSynchronous();
	if (!LoadedConfig)
	{
		UE_LOG(LogTemp, Error, TEXT("[UIManager] Failed to load UI Config from Project Settings: %s"), 
			*Settings->UIConfigAsset.ToSoftObjectPath().ToString());
		return;
	}

	// Use our SetConfig method to properly store the config
	SetConfig(LoadedConfig);
	UE_LOG(LogTemp, Warning, TEXT("[UIManager] Config loaded from Project Settings: %s"), *LoadedConfig->GetName());
}

void UTLDUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Initialize called"));

	Super::Initialize(Collection);
	
	// Load config from Project Settings during subsystem initialization
	LoadConfigFromProjectSettings();
}



void UTLDUIManager::Deinitialize()
{
//	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Deinitialize called"));
	
	// Clean up all bindings first
	UnbindFromChannel();
	UnbindFromAttributeChanges();

	// Remove all live widgets from viewport and clear references
	for (auto& Pair : LiveWidgets)
	{
		if (UUserWidget* W = Pair.Value.Get())
		{
			W->RemoveFromParent();
		}
	}
	LiveWidgets.Empty();
	Pooled.Empty();

	// Clear cached references
	HUDRoot.Reset();
	HUDData = nullptr;
	
	// Clear config references
	CurrentConfig = nullptr;
}

/* ========================================================================== */
/*  Config                                                                    */
/* ========================================================================== */

void UTLDUIManager::SetConfig(UTLDUiConfig* InConfig)
{
	// FIXED: Store in CurrentConfig (strong reference) which is what IsConfigLoaded() checks
	CurrentConfig = InConfig;
	
	if (InConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Config set successfully: %s"), *InConfig->GetName());
		
		// Debug: Log available overlays in the config
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Config contains %d overlay configurations:"), InConfig->Overlays.Num());
		for (const auto& Pair : InConfig->Overlays)
		{
			UE_LOG(LogTemp, Log, TEXT("  - Overlay %d: %s"), 
				(int32)Pair.Key, 
				Pair.Value.WidgetClass ? *Pair.Value.WidgetClass->GetName() : TEXT("No Widget Class"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] Config set to nullptr!"));
	}
}

void UTLDUIManager::RefreshHealthUI()
{
	if (!HUDRoot.Get()) return;

	if (UTLDGameState* GS = GetGameInstance()->GetSubsystem<UTLDGameState>())
	{
		UTLDUIPayload_Health* Payload = NewObject<UTLDUIPayload_Health>(this);
		Payload->Cur = GS->GetStateValueOrZero(TAG_State_Health_Current);
		Payload->Max = GS->GetStateValueOrZero(TAG_State_Health_Max);

		HUD_SendEvent("UI.HUD.Health.Update", Payload);
	}
}

/* ========================================================================== */
/*  Utilities                                                                 */
/* ========================================================================== */

UWorld* UTLDUIManager::GetWorldSafe() const
{
	return GetWorld();
}

APlayerController* UTLDUIManager::GetPC() const
{
	if (UWorld* W = GetWorldSafe())
	{
		return W->GetFirstPlayerController();
	}
	return nullptr;
}

UUserWidget* UTLDUIManager::CreateOrReuse(ETLDUIOverlay Overlay, const FTLDOverlayConfig& Cfg)
{
	// Check if we already have a live widget for this overlay
	if (UUserWidget* Live = LiveWidgets.FindRef(Overlay).Get())
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Reusing existing live widget for overlay: %d"), (int32)Overlay);
		return Live;
	}

	// Try to reuse from object pool
	if (TArray<TWeakObjectPtr<UUserWidget>>* Pool = Pooled.Find(Overlay))
	{
		// Search backwards for efficiency (most recently pooled first)
		for (int32 i = Pool->Num() - 1; i >= 0; --i)
		{
			if (UUserWidget* P = (*Pool)[i].Get())
			{
				Pool->RemoveAt(i);
				LiveWidgets.Add(Overlay, P);
				UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Reusing pooled widget for overlay: %d"), (int32)Overlay);
				return P;
			}
			// Remove dead weak pointers while we're at it
			Pool->RemoveAt(i);
		}
	}

	// Create a fresh widget instance
	if (!Cfg.WidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] No WidgetClass set for overlay: %d"), (int32)Overlay);
		return nullptr;
	}
	
	if (UWorld* W = GetWorldSafe())
	{
		UUserWidget* NewW = CreateWidget<UUserWidget>(W, Cfg.WidgetClass);
		if (NewW)
		{
			LiveWidgets.Add(Overlay, NewW);
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Created fresh widget for overlay: %d, Class: %s"), 
				(int32)Overlay, *Cfg.WidgetClass->GetName());
			return NewW;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] Failed to create widget for overlay: %d"), (int32)Overlay);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] No valid world context for widget creation"));
	}
	return nullptr;
}

void UTLDUIManager::AddToViewportIfNeeded(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget) return;
	
	// Only add to viewport if not already there
	if (!Widget->IsInViewport()) 
	{
		Widget->AddToViewport(ZOrder);
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Added widget to viewport with ZOrder: %d"), ZOrder);
	}
	
	// Ensure visibility is set correctly
	Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UTLDUIManager::ApplyInputMode(ETLDUIInputMode Mode)
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] No PlayerController available for input mode change"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Applying input mode: %d"), (int32)Mode);
	
	switch (Mode)
	{
	case ETLDUIInputMode::GameOnly:
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
		break;

	case ETLDUIInputMode::UIOnly:
	{
		FInputModeUIOnly IM;
		IM.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(IM);
		break;
	}

	case ETLDUIInputMode::GameAndUI:
	{
		FInputModeGameAndUI IM;
		IM.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(IM);
		break;
	}
	default:
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Unknown input mode: %d"), (int32)Mode);
		break;
	}
}

void UTLDUIManager::PlayShowTransition(UUserWidget* Widget, ETLDUITransition /*Tr*/)
{
	if (!Widget) return;
	
	// MVP: Just set visibility immediately - no animation yet
	Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UTLDUIManager::PlayHideTransition(UUserWidget* Widget, ETLDUITransition /*Tr*/, TFunction<void()> OnFinished)
{
	if (!Widget) 
	{ 
		if (OnFinished) OnFinished(); 
		return; 
	}
	
	// MVP: Immediately call completion callback - no animation yet
	if (OnFinished) OnFinished();
}

void UTLDUIManager::ReturnToPoolOrDiscard(ETLDUIOverlay Overlay, UUserWidget* Widget, bool bKeepInPool)
{
	if (!Widget) return;
	
	// Remove from viewport and hide
	Widget->RemoveFromParent();
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	
	// Remove from live widgets tracking
	LiveWidgets.Remove(Overlay);
	
	// Either pool for reuse or let it be garbage collected
	if (bKeepInPool) 
	{
		Pooled.FindOrAdd(Overlay).Add(Widget);
	}
	
	UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Returned widget to pool (keep: %s) for overlay: %d"), 
		bKeepInPool ? TEXT("true") : TEXT("false"), (int32)Overlay);
}

/* ========================================================================== */
/*  Overlays                                                                  */
/* ========================================================================== */

UUserWidget* UTLDUIManager::ShowOverlay(ETLDUIOverlay Overlay)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] ShowOverlay called for: %d"), (int32)Overlay);
	
	// FIXED: Use CurrentConfig instead of Config.Get()
	if (!CurrentConfig) 
	{
		UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] No UI Config available! Make sure config is set in Project Settings."));
		return nullptr;
	}

	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Using config: %s"), *CurrentConfig->GetName());

	// Find the overlay configuration
	if (const FTLDOverlayConfig* Cfg = CurrentConfig->Overlays.Find(Overlay))
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Found config for overlay: %d"), (int32)Overlay);
		
		// Create or reuse the widget
		if (UUserWidget* W = CreateOrReuse(Overlay, *Cfg))
		{
			// Add to viewport with proper Z-order
			AddToViewportIfNeeded(W, Cfg->ZOrder);
			
			// Apply input mode settings
			ApplyInputMode(Cfg->InputMode);
			
			// Play show transition (currently just immediate visibility)
			PlayShowTransition(W, Cfg->ShowTransition);

			// Cache HUD reference if this is the HUD overlay
			if (Overlay == ETLDUIOverlay::HUD)
			{
				CacheHUDIfPresent();
			}
			
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Successfully showed overlay: %d"), (int32)Overlay);
			return W;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] Failed to create widget for overlay: %d"), (int32)Overlay);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[UTLDUIManager] No config found for overlay: %d in config asset"), (int32)Overlay);
		
		// Debug: print all available overlay configs to help troubleshooting
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Available overlays in config:"));
		for (const auto& Pair : CurrentConfig->Overlays)
		{
			UE_LOG(LogTemp, Log, TEXT("  - Overlay: %d"), (int32)Pair.Key);
		}
	}
	return nullptr;
}

void UTLDUIManager::HideOverlay(ETLDUIOverlay Overlay)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] HideOverlay called for: %d"), (int32)Overlay);
	
	// FIXED: Use CurrentConfig instead of Config.Get()
	if (!CurrentConfig) 
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] No config available for HideOverlay"));
		return;
	}

	if (const FTLDOverlayConfig* Cfg = CurrentConfig->Overlays.Find(Overlay))
	{
		if (UUserWidget* W = LiveWidgets.FindRef(Overlay).Get())
		{
			// Play hide transition with completion callback
			PlayHideTransition(W, Cfg->HideTransition, [this, Overlay, Cfg, W]()
			{
				// Restore game-only input if the overlay changed input mode
				if (Cfg->InputMode != ETLDUIInputMode::GameOnly)
				{
					ApplyInputMode(ETLDUIInputMode::GameOnly);
				}

				// Special handling for pause overlay - unpause the game
				if (Overlay == ETLDUIOverlay::Pause)
				{
					if (UWorld* World = GetWorldSafe())
					{
						UGameplayStatics::SetGamePaused(World, false);
					}
				}
				
				// Return to pool or discard based on persistence setting
				ReturnToPoolOrDiscard(Overlay, W, Cfg->bPersistent);
			});
			
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Successfully hid overlay: %d"), (int32)Overlay);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] No live widget found for overlay: %d"), (int32)Overlay);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] No config found for overlay: %d"), (int32)Overlay);
	}
}

void UTLDUIManager::ToggleOverlay(ETLDUIOverlay Overlay)
{
	if (IsOverlayVisible(Overlay)) 
	{
		HideOverlay(Overlay);
	}
	else                          
	{
		ShowOverlay(Overlay);
	}
}

bool UTLDUIManager::IsOverlayVisible(ETLDUIOverlay Overlay) const
{
	if (UUserWidget* W = LiveWidgets.FindRef(Overlay).Get())
	{
		return W->IsVisible();
	}
	return false;
}

UUserWidget* UTLDUIManager::GetOverlay(ETLDUIOverlay OverlayType)
{
	return LiveWidgets.FindRef(OverlayType).Get();
}

/* ========================================================================== */
/*  Legacy generic push (still supported)                                     */
/* ========================================================================== */

void UTLDUIManager::UpdateOverlayData(ETLDUIOverlay Overlay, UObject* DataObject)
{
	if (UUserWidget* W = LiveWidgets.FindRef(Overlay).Get())
	{
		// Call OnDataUpdated function if it exists on the widget
		static const FName FuncName(TEXT("OnDataUpdated"));
		if (UFunction* Fn = W->FindFunction(FuncName))
		{
			struct { UObject* Arg; } Params{ DataObject };
			W->ProcessEvent(Fn, &Params);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Widget %s doesn't have OnDataUpdated function"), 
				*W->GetClass()->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] No live widget found for overlay %d to update data"), 
			(int32)Overlay);
	}
}

/* ========================================================================== */
/*  HUD spawn + bind                                                          */
/* ========================================================================== */

void UTLDUIManager::ShowHUDAndBind(APawn* ForPawn)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] ShowHUDAndBind called"));
	
	// Show the HUD overlay first
	ShowOverlay(ETLDUIOverlay::HUD);
	CacheHUDIfPresent();

	// Auto-resolve pawn if not provided
	if (!ForPawn)
	{
		if (APlayerController* PC = GetPC())
		{
			ForPawn = PC->GetPawn();
			UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Auto-resolved pawn: %s"), 
				ForPawn ? *ForPawn->GetName() : TEXT("nullptr"));
		}
	}

	// Bind to gameplay systems
	BindToAttributeChanges(ResolveASC(ForPawn));
	BindToChannel(ResolveChannel(ForPawn));

	// Send initial health data if we have both HUD and ASC
	if (HUDRoot.IsValid() && CachedASC.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Sending initial health data to HUD"));
		
		UTLDUIPayload_Health* P = NewObject<UTLDUIPayload_Health>(this);
		P->Cur = CachedASC->GetNumericAttribute(UTLDAttributeSet_Combat::GetHealthAttribute());
		P->Max = FMath::Max(0.01f, CachedASC->GetNumericAttribute(UTLDAttributeSet_Combat::GetMaxHealthAttribute()));
		HUD_SendEvent("UI.HUD.Health.Update", P);
	}
	else
	{
		// Log what's missing for debugging
		if (!HUDRoot.IsValid())
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] HUDRoot is not valid - HUD widget may not be a TLDDesignerWidgetBase"));
		if (!CachedASC.IsValid())
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] CachedASC is not valid - pawn may not have AbilitySystemComponent"));
	}
}

/* ========================================================================== */
/*  Cache / Resolve helpers                                                   */
/* ========================================================================== */

void UTLDUIManager::CacheHUDIfPresent()
{
	if (UUserWidget* W = LiveWidgets.FindRef(ETLDUIOverlay::HUD).Get())
	{
		// Try to cast to our event-based widget base class
		HUDRoot = Cast<UTLDDesignerWidgetBase>(W);
		if (HUDRoot.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Cached HUD root widget: %s"), *W->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] HUD widget is not a UTLDDesignerWidgetBase: %s"), 
				*W->GetClass()->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] No HUD widget to cache"));
	}
}

UAbilitySystemComponent* UTLDUIManager::ResolveASC(APawn* Pawn) const
{
	if (!Pawn) 
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] No pawn provided to ResolveASC"));
		return nullptr;
	}
	
	// Check if the pawn implements IAbilitySystemInterface
	if (IAbilitySystemInterface* IFace = Cast<IAbilitySystemInterface>(Pawn))
	{
		UAbilitySystemComponent* ASC = IFace->GetAbilitySystemComponent();
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Resolved ASC from pawn: %s -> %s"), 
			*Pawn->GetName(), ASC ? *ASC->GetName() : TEXT("nullptr"));
		return ASC;
	}
	
	UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Pawn does not implement IAbilitySystemInterface: %s"), 
		*Pawn->GetName());
	return nullptr;
}

UTLDChannelComponent* UTLDUIManager::ResolveChannel(APawn* Pawn) const
{
	if (!Pawn)
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] No pawn provided to ResolveChannel"));
		return nullptr;
	}
	
	// Look for channel component on the pawn
	UTLDChannelComponent* Channel = Pawn->FindComponentByClass<UTLDChannelComponent>();
	UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Resolved Channel from pawn: %s -> %s"), 
		*Pawn->GetName(), Channel ? *Channel->GetName() : TEXT("nullptr"));
	return Channel;
}

/* ========================================================================== */
/*  GAS binding (Health only)                                                 */
/* ========================================================================== */

void UTLDUIManager::BindToAttributeChanges(UAbilitySystemComponent* ASC)
{
	if (!ASC) 
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] No ASC provided to BindToAttributeChanges"));
		return;
	}

	// Idempotent - don't bind twice to the same ASC
	if (CachedASC.Get() == ASC && HealthChangedHandle.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Already bound to this ASC"));
		return;
	}

	// Clean up any existing bindings first
	UnbindFromAttributeChanges();
	CachedASC = ASC;

	// Bind to health attribute changes
	HealthChangedHandle =
		ASC->GetGameplayAttributeValueChangeDelegate(UTLDAttributeSet_Combat::GetHealthAttribute())
			.AddUObject(this, &UTLDUIManager::HandleHealthChanged);
			
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Bound to attribute changes for ASC: %s"), *ASC->GetName());
}

void UTLDUIManager::UnbindFromAttributeChanges()
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(
				UTLDAttributeSet_Combat::GetHealthAttribute()).Remove(HealthChangedHandle);
			HealthChangedHandle.Reset();
			UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Unbound from attribute changes"));
		}
	}
	CachedASC.Reset();
}

void UTLDUIManager::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	const float Cur = Data.NewValue;

	float Max = 0.f;
	if (CachedASC.IsValid())
	{
		Max = CachedASC->GetNumericAttribute(UTLDAttributeSet_Combat::GetMaxHealthAttribute());
	}

	// 1) Drive HUD (MainHUD -> HealthSubHUD -> ProgressBar)
	if (UTLDMainHUD* HUD = Cast<UTLDMainHUD>(GetOverlay(ETLDUIOverlay::HUD)))
	{
		HUD->UpdateHealth(Cur, Max);
	}

	// 2) Mirror into GameState for other systems
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UTLDGameState* GS = GI->GetSubsystem<UTLDGameState>())
		{
			GS->SetStateValue(TAG_State_Health_Current, Cur);
			GS->SetStateValue(TAG_State_Health_Max, Max);
			GS->SetStateValue(TAG_State_Health_Percent, (Max > 0.f) ? Cur / Max : 0.f);
		}
	}

	// (Optional) keep your event bus calls if you’re using them elsewhere
	// if (HUDRoot.IsValid()) { ... HUD_SendEvent("UI.HUD.Health.Update", Payload) ... }
}
/* ========================================================================== */
/*  Channel (cast bar)                                                        */
/* ========================================================================== */

void UTLDUIManager::BindToChannel(UTLDChannelComponent* Channel)
{
	if (!Channel) 
	{
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] No Channel provided to BindToChannel"));
		return;
	}
	
	// Don't bind twice to the same channel
	if (CachedChannel.Get() == Channel) return;

	// Clean up existing bindings
	UnbindFromChannel();
	CachedChannel = Channel;

	// Bind to all channel events
	Channel->OnChannelStarted.AddDynamic(this, &UTLDUIManager::OnChannelStarted_UI);
	Channel->OnChannelTick   .AddDynamic(this, &UTLDUIManager::OnChannelTick_UI);
	Channel->OnChannelEnded  .AddDynamic(this, &UTLDUIManager::OnChannelEnded_UI);
	
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Bound to channel component: %s"), *Channel->GetName());
}

void UTLDUIManager::UnbindFromChannel()
{
	if (UTLDChannelComponent* Ch = CachedChannel.Get())
	{
		Ch->OnChannelStarted.RemoveAll(this);
		Ch->OnChannelTick   .RemoveAll(this);
		Ch->OnChannelEnded  .RemoveAll(this);
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Unbound from channel component"));
	}
	CachedChannel.Reset();
}

void UTLDUIManager::OnChannelStarted_UI(const FChannelSpec& Spec)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Channel started: %s"), *Spec.DisplayName.ToString());
	
	// Create channel payload for show event
	UTLDUIPayload_Channel* P = NewObject<UTLDUIPayload_Channel>(this);
	P->Label = Spec.DisplayName;
	P->Icon  = Spec.Icon;
	P->bShow = true;

	HUD_SendEvent("UI.HUD.Channel.Show", P);
}

void UTLDUIManager::OnChannelTick_UI(float Elapsed, float Total)
{
	// Calculate progress percentage
	float Progress = (Total > 0.f) ? FMath::Clamp(Elapsed / Total, 0.f, 1.f) : 0.f;
	
	// Create progress update payload
	UTLDUIPayload_Channel* P = NewObject<UTLDUIPayload_Channel>(this);
	P->Progress01 = Progress;

	HUD_SendEvent("UI.HUD.Channel.Progress", P);
}

void UTLDUIManager::OnChannelEnded_UI(bool bSuccess, EChannelCancelReason /*Reason*/)
{
	UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Channel ended: Success=%s"), bSuccess ? TEXT("true") : TEXT("false"));
	
	// Create channel hide payload
	UTLDUIPayload_Channel* P = NewObject<UTLDUIPayload_Channel>(this);
	P->bShow    = false;
	P->bSuccess = bSuccess;

	HUD_SendEvent("UI.HUD.Channel.Hide", P);
}

/* ========================================================================== */
/*  Legacy HUD data (optional)                                                */
/* ========================================================================== */

UTLDCombatUIData* UTLDUIManager::GetOrCreateHUDData()
{
	if (!HUDData) 
	{
		HUDData = NewObject<UTLDCombatUIData>(this);
	}
	return HUDData;
}

/* ========================================================================== */
/*  Wolf overlay helper                                                       */
/* ========================================================================== */

void UTLDUIManager::SetWolfOverlayVisible(bool bVisible)
{
	if (bVisible) 
	{
		ShowOverlay(ETLDUIOverlay::Wolf);
	}
	else          
	{
		HideOverlay(ETLDUIOverlay::Wolf);
	}
}

/* ========================================================================== */
/*  HUD event bus dispatch                                                    */
/* ========================================================================== */

void UTLDUIManager::HUD_SendEvent(FName EventTag, UObject* Payload)
{
	// Ensure we have a valid HUD root widget to send events to
	if (!HUDRoot.IsValid()) 
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] Cannot send event '%s' - no HUD root"), *EventTag.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Sending event: %s"), *EventTag.ToString());

	// Call ApplyUIEvent function on the HUD root widget
	static const FName FN_ApplyUIEvent(TEXT("ApplyUIEvent"));
	if (UFunction* Fn = HUDRoot->FindFunction(FN_ApplyUIEvent))
	{
		// Create parameter struct for the function call
		struct { FName InTag; UObject* InPayload; } Params{ EventTag, Payload };
		HUDRoot->ProcessEvent(Fn, &Params);
		UE_LOG(LogTemp, Log, TEXT("[UTLDUIManager] Event sent successfully: %s"), *EventTag.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[UTLDUIManager] ApplyUIEvent function not found on HUD root widget"));
	}
}