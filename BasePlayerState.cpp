#include "BasePlayerState.h"

#include "GameCore/BaseGameState.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/Match/RunStatisticsSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Net/UnrealNetwork.h"

ABasePlayerState::ABasePlayerState()
{
}

void ABasePlayerState::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT("[Loadout] PS 생성 - Map=%s Auth=%d Hero=%s"),
		*GetWorld()->GetMapName(),
		HasAuthority() ? 1 : 0,
		*UEnum::GetValueAsString(Loadout.HeroType));
}

void ABasePlayerState::CopyProperties(APlayerState* NewPlayerState)
{
	Super::CopyProperties(NewPlayerState);

	if (ABasePlayerState* NewPS = Cast<ABasePlayerState>(NewPlayerState))
	{
		NewPS->Loadout = Loadout;
		NewPS->RoomJoinOrder = RoomJoinOrder;
		NewPS->RunStatistics = RunStatistics;
		// 섹터 이동으로 모은 골드가 날아가지 않게 같이 넘긴다.
		NewPS->Gold = Gold;
		NewPS->LastBroadcastGold = Gold;
		NewPS->GeneratedWeaponHistory = GeneratedWeaponHistory;
		NewPS->TotalGeneratedWeaponCount = TotalGeneratedWeaponCount;
		NewPS->GeneratedRarityCounts = GeneratedRarityCounts;
		NewPS->GeneratedCategoryCounts = GeneratedCategoryCounts;
		NewPS->ShopPullsSinceLegendary = ShopPullsSinceLegendary;
		NewPS->ShopPullsSinceEpic = ShopPullsSinceEpic;
	}
}

void ABasePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABasePlayerState, Loadout);
	DOREPLIFETIME(ABasePlayerState, RoomJoinOrder);
	DOREPLIFETIME_CONDITION(ABasePlayerState, RunStatistics, COND_OwnerOnly);
	DOREPLIFETIME(ABasePlayerState, Vitals);
	DOREPLIFETIME(ABasePlayerState, Gold);
	DOREPLIFETIME_CONDITION(ABasePlayerState, GeneratedWeaponHistory, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABasePlayerState, TotalGeneratedWeaponCount, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABasePlayerState, GeneratedRarityCounts, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABasePlayerState, GeneratedCategoryCounts, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABasePlayerState, ShopPullsSinceLegendary, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABasePlayerState, ShopPullsSinceEpic, COND_OwnerOnly);
}

void ABasePlayerState::SetRoomJoinOrder(int32 NewOrder)
{
	if (!HasAuthority() || RoomJoinOrder != INDEX_NONE || NewOrder < 0)
	{
		return;
	}
	RoomJoinOrder = NewOrder;
	NotifyLoadoutChanged();
}

void ABasePlayerState::OnRep_RoomJoinOrder()
{
	NotifyLoadoutChanged();
}

void ABasePlayerState::ResetRunData()
{
	if (!HasAuthority())
	{
		return;
	}

	// 신규 PlayerState와 동일한 시작 골드. 서버 델리게이트는 초기화로 알린다.
	Gold = GetDefault<ABasePlayerState>()->Gold;
	NotifyGoldChanged(0);
	SetVitals(0.0f, 0.0f, 1.0f);
	GeneratedWeaponHistory.Reset();
	TotalGeneratedWeaponCount = 0;
	GeneratedRarityCounts = FGeneratedWeaponRarityCounts{};
	GeneratedCategoryCounts = FGeneratedWeaponCategoryCounts{};
	ShopPullsSinceLegendary = 0;
	ShopPullsSinceEpic = 0;
	ResetRunStatistics();
	ForceNetUpdate();
}

bool ABasePlayerState::CanRecordRunStatistics() const
{
	const UGameInstance* GI = GetGameInstance();
	const URunStatisticsSubsystem* Clock = GI ? GI->GetSubsystem<URunStatisticsSubsystem>() : nullptr;
	return HasAuthority() && !IsInactive() && !RunStatistics.bFinalized && Clock && Clock->IsCollecting();
}

void ABasePlayerState::RecordDamageDealt(float ActualDamage, bool bBoss, bool bKilled)
{
	if (!CanRecordRunStatistics() || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f)
	{
		return;
	}
	RunStatistics.TotalDamage += ActualDamage;
	if (bBoss)
	{
		RunStatistics.BossDamage += ActualDamage;
	}
	if (bKilled)
	{
		++RunStatistics.KillCount;
	}
	OnRunStatisticsChanged.Broadcast();
}

void ABasePlayerState::RecordDamageTaken(float ActualDamage)
{
	if (!CanRecordRunStatistics() || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f)
	{
		return;
	}
	RunStatistics.DamageTaken += ActualDamage;
	OnRunStatisticsChanged.Broadcast();
}

void ABasePlayerState::RecordTeamRevive()
{
	if (CanRecordRunStatistics())
	{
		++RunStatistics.ReviveCount;
		OnRunStatisticsChanged.Broadcast();
	}
}

void ABasePlayerState::FinalizeRunStatistics(float ClearDurationSeconds)
{
	if (!HasAuthority() || RunStatistics.bFinalized)
	{
		return;
	}
	RunStatistics.ClearDurationSeconds = FMath::Max(0.0f, ClearDurationSeconds);
	RunStatistics.bFinalized = true;
	OnRunStatisticsChanged.Broadcast();
	ForceNetUpdate();
}

void ABasePlayerState::ResetRunStatistics()
{
	if (HasAuthority())
	{
		RunStatistics = FRunStatistics{};
		OnRunStatisticsChanged.Broadcast();
		ForceNetUpdate();
	}
}

void ABasePlayerState::OnRep_RunStatistics()
{
	OnRunStatisticsChanged.Broadcast();
}

void ABasePlayerState::RecordGeneratedWeapon(
	EGunRarity Rarity,
	EWeaponCategory WeaponCategory,
	EWeaponGenerationSource Source)
{
	if (!HasAuthority())
	{
		return;
	}

	++TotalGeneratedWeaponCount;
	switch (Rarity)
	{
	case EGunRarity::Common: ++GeneratedRarityCounts.Common; break;
	case EGunRarity::Uncommon: ++GeneratedRarityCounts.Uncommon; break;
	case EGunRarity::Epic: ++GeneratedRarityCounts.Epic; break;
	case EGunRarity::Legendary: ++GeneratedRarityCounts.Legendary; break;
	}

	switch (WeaponCategory)
	{
	case EWeaponCategory::Rifle: ++GeneratedCategoryCounts.Rifle; break;
	case EWeaponCategory::GrenadeLauncher: ++GeneratedCategoryCounts.GrenadeLauncher; break;
	case EWeaponCategory::SniperRifle: ++GeneratedCategoryCounts.SniperRifle; break;
	}

	if (Source == EWeaponGenerationSource::Shop)
	{
		ShopPullsSinceLegendary = Rarity == EGunRarity::Legendary
			? 0
			: ShopPullsSinceLegendary + 1;
		ShopPullsSinceEpic = static_cast<uint8>(Rarity) >= static_cast<uint8>(EGunRarity::Epic)
			? 0
			: ShopPullsSinceEpic + 1;
	}

	FGeneratedWeaponRecord& Record = GeneratedWeaponHistory.AddDefaulted_GetRef();
	Record.Sequence = TotalGeneratedWeaponCount;
	Record.Source = Source;
	Record.Rarity = Rarity;
	Record.WeaponCategory = WeaponCategory;

	const int32 Overflow = GeneratedWeaponHistory.Num() - MaxRecentGeneratedWeaponRecords;
	if (Overflow > 0)
	{
		GeneratedWeaponHistory.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

int32 ABasePlayerState::GetGeneratedRarityCount(EGunRarity Rarity) const
{
	switch (Rarity)
	{
	case EGunRarity::Common: return GeneratedRarityCounts.Common;
	case EGunRarity::Uncommon: return GeneratedRarityCounts.Uncommon;
	case EGunRarity::Epic: return GeneratedRarityCounts.Epic;
	case EGunRarity::Legendary: return GeneratedRarityCounts.Legendary;
	default: return 0;
	}
}

int32 ABasePlayerState::GetGeneratedCategoryCount(EWeaponCategory WeaponCategory) const
{
	switch (WeaponCategory)
	{
	case EWeaponCategory::Rifle: return GeneratedCategoryCounts.Rifle;
	case EWeaponCategory::GrenadeLauncher: return GeneratedCategoryCounts.GrenadeLauncher;
	case EWeaponCategory::SniperRifle: return GeneratedCategoryCounts.SniperRifle;
	default: return 0;
	}
}

float ABasePlayerState::GetHealthPercent() const
{
	return Vitals.MaxHealth > 0.0f ? Vitals.Health / Vitals.MaxHealth : 0.0f;
}

void ABasePlayerState::SetVitals(float NewHealth, float NewMaxHealth, float NewHealthCapMultiplier)
{
	if (FMath::IsNearlyEqual(Vitals.Health, NewHealth)
		&& FMath::IsNearlyEqual(Vitals.MaxHealth, NewMaxHealth)
		&& FMath::IsNearlyEqual(Vitals.HealthCapMultiplier, NewHealthCapMultiplier))
	{
		return;
	}

	Vitals.Health = NewHealth;
	Vitals.MaxHealth = NewMaxHealth;
	Vitals.HealthCapMultiplier = NewHealthCapMultiplier;

	UE_LOG(LogTemp, Warning, TEXT("[SetVitals] Server updated Vitals and Broadcasting! Health: %f"), Vitals.Health);
	
	// 서버(및 스탠드얼론)용. 클라이언트는 OnRep_Vitals가 대신 쏜다.
	OnVitalsChanged.Broadcast(Vitals.Health, Vitals.MaxHealth, Vitals.HealthCapMultiplier);
}

void ABasePlayerState::OnRep_Vitals()
{
	UE_LOG(LogTemp, Warning, TEXT("[OnRep_Vitals] Client received Vitals! Health: %f"), Vitals.Health);
	OnVitalsChanged.Broadcast(Vitals.Health, Vitals.MaxHealth, Vitals.HealthCapMultiplier);
}

void ABasePlayerState::AddGold(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return;
	}

	// 장시간 파밍에서 누적값이 int32를 넘겨 음수로 뒤집히는 것을 막는다.
	const int32 Granted = FMath::Min(Amount, TNumericLimits<int32>::Max() - Gold);
	if (Granted <= 0)
	{
		return;
	}

	Gold += Granted;

	// 서버(및 스탠드얼론)용. 클라이언트는 OnRep_Gold가 대신 쏜다.
	NotifyGoldChanged(Granted);
}

bool ABasePlayerState::TrySpendGold(int32 Cost)
{
	// 결제는 반드시 서버에서만 확정한다. 클라 호출은 조용히 실패시킨다.
	if (!HasAuthority() || Cost < 0)
	{
		return false;
	}

	if (Cost == 0)
	{
		return true;
	}

	if (Gold < Cost)
	{
		return false;
	}

	Gold -= Cost;
	NotifyGoldChanged(-Cost);
	return true;
}

void ABasePlayerState::OnRep_Gold()
{
	// 접속/트래블 직후 첫 동기화는 획득이 아니다.
	const int32 Delta = bGoldReplicatedOnce ? Gold - LastBroadcastGold : 0;
	bGoldReplicatedOnce = true;
	NotifyGoldChanged(Delta);
}

void ABasePlayerState::NotifyGoldChanged(int32 Delta)
{
	LastBroadcastGold = Gold;
	OnGoldChanged.Broadcast(Gold, Delta);
}

void ABasePlayerState::SetHeroType(EDLHero NewHeroType)
{
	Loadout.HeroType = NewHeroType;
	NotifyLoadoutChanged();
}

void ABasePlayerState::SetPlayerNickname(const FString& NewName)
{
	Loadout.PlayerName = NewName;
	NotifyLoadoutChanged();
}

void ABasePlayerState::OnRep_Loadout()
{
	NotifyLoadoutChanged();
}

bool ABasePlayerState::IsLocal() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (const ULocalPlayer* LocalPlayer = World->GetFirstLocalPlayerFromController())
	{
		const FUniqueNetIdRepl& LocalId = LocalPlayer->GetPreferredUniqueNetId();
		if (LocalId.IsValid() && GetUniqueId().IsValid())
		{
			return GetUniqueId() == LocalId;
		}
	}

	// 온라인 서브시스템이 없는 환경에서는 UniqueId가 비어 있다. 포인터 비교로 떨어진다.
	const APlayerController* PC = World->GetFirstPlayerController();
	return PC && PC->PlayerState == this;
}

void ABasePlayerState::OnRep_UniqueId()
{
	Super::OnRep_UniqueId();

	if (const UWorld* World = GetWorld())
	{
		if (ABasePlayerController* PC = Cast<ABasePlayerController>(World->GetFirstPlayerController()))
		{
			PC->UIInitialize.Broadcast();
		}
	}
}

void ABasePlayerState::NotifyLoadoutChanged()
{
	if (const UWorld* World = GetWorld())
	{
		if (ABaseGameState* GS = World->GetGameState<ABaseGameState>())
		{
			GS->OnRoomPlayersChanged.Broadcast();
		}
	}
}
