#include "Utils.h"
#include <numeric>
#include "Inventory.h"
#include "GameMode.h"
using namespace Utils;

AFortPickup* Inventory::SpawnPickup(FVector Loc, UFortItemDefinition* Def, EFortPickupSourceTypeFlag SourceTypeFlag, int Count, int LoadedAmmo, EFortPickupSpawnSource SpawnSource, AFortPlayerPawn* Owner)
{
	if (!Def)
		return nullptr;
	FTransform Transform{};
	Transform.Translation = Loc;
	Transform.Scale3D = FVector{ 1,1,1 };
	AFortPickup* Pickup = (AFortPickup*)(UGameplayStatics::FinishSpawningActor(UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), AFortPickupAthena::StaticClass(), (FTransform&)Transform, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, nullptr), (FTransform&)Transform));
	if (!Pickup)
		return nullptr;
	Pickup->bRandomRotation = true;

	Pickup->PrimaryPickupItemEntry.ItemDefinition = Def;
	Pickup->PrimaryPickupItemEntry.Count = Count;
	Pickup->PrimaryPickupItemEntry.LoadedAmmo = LoadedAmmo;
	Pickup->OnRep_PrimaryPickupItemEntry();

	Pickup->TossPickup(Loc, Owner, -1, true, true, SourceTypeFlag, SpawnSource);

	if (SourceTypeFlag == EFortPickupSourceTypeFlag::Container)
	{
		Pickup->bTossedFromContainer = true;
		Pickup->OnRep_TossedFromContainer();
	}

	return Pickup;
}

void Inventory::UpdateItem(AFortPlayerControllerAthena* PC, FFortItemEntry& Entry)
{
	if (!PC)
		return;

	for (auto Item : PC->WorldInventory->Inventory.ItemInstances)
	{
		if (Item->ItemEntry.ItemGuid == Entry.ItemGuid)
		{
			Item->ItemEntry.Count = Entry.Count;
			Item->ItemEntry.LoadedAmmo = Entry.LoadedAmmo;
			break;
		}
	}
}

bool IsPrimary(UFortItemDefinition* Def)
{
	return Def->IsA(UFortWeaponRangedItemDefinition::StaticClass()) || Def->IsA(UFortConsumableItemDefinition::StaticClass()) || Def->IsA(UFortWeaponMeleeItemDefinition::StaticClass());
}

int GetUsedSlots(AFortPlayerControllerAthena* PC)
{
	int ret = 0;
	for (size_t i = 0; i < PC->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
	{
		auto& Entry = PC->WorldInventory->Inventory.ReplicatedEntries[i];
		if (IsPrimary(Entry.ItemDefinition))
			ret++;
	}
	return ret-1;
}

void Inventory::GiveItem(AFortPlayerControllerAthena* PC, UFortItemDefinition* Item, int Count, int LoadedAmmo, bool Stack)
{
	if (!PC || !PC->WorldInventory || !Item)
		return;
	auto MaxStackSize = UFortScalableFloatUtils::GetValueAtLevel(Item->MaxStackSize, 0);
	if (Stack)
	{
		for (auto& ItemEntry : PC->WorldInventory->Inventory.ReplicatedEntries)
		{
			if (ItemEntry.ItemDefinition == Item)
			{
				ItemEntry.Count += Count;
				if (ItemEntry.Count > MaxStackSize)
				{
					if (Item->bAllowMultipleStacks && GetUsedSlots(PC) < 5)
					{
						GiveItem(PC, Item, ItemEntry.Count - MaxStackSize, LoadedAmmo, false);
					}
					else
					{
						SpawnPickup(PC->Pawn->K2_GetActorLocation(), Item, EFortPickupSourceTypeFlag::Player, ItemEntry.Count - MaxStackSize, 0, EFortPickupSpawnSource::Unset);
					}
					ItemEntry.Count = MaxStackSize;
				}
				PC->WorldInventory->Inventory.MarkItemDirty(ItemEntry);
				UpdateItem(PC, ItemEntry);
				PC->WorldInventory->HandleInventoryLocalUpdate();
				return;
			}
		}
		GiveItem(PC, Item, Count, LoadedAmmo, false);
		return;
	}

	auto WorldItem = (UFortWorldItem*)Item->CreateTemporaryItemInstanceBP(Count, 0);
	WorldItem->SetOwningControllerForTemporaryItem(PC);
	WorldItem->ItemEntry.LoadedAmmo = LoadedAmmo;
	WorldItem->ItemEntry.ItemDefinition = Item;
	WorldItem->ItemEntry.Count = Count;
	PC->WorldInventory->Inventory.ReplicatedEntries.Add(WorldItem->ItemEntry);
	PC->WorldInventory->Inventory.ItemInstances.Add(WorldItem);
	//PC->WorldInventory->Inventory.MarkArrayDirty();
	PC->WorldInventory->Inventory.MarkItemDirty(WorldItem->ItemEntry);
	PC->WorldInventory->bRequiresLocalUpdate = true;
	PC->WorldInventory->HandleInventoryLocalUpdate();
	PC->HandleWorldInventoryLocalUpdate();

	auto AmmoData = ((UFortWorldItemDefinition*)Item)->GetAmmoWorldItemDefinition_BP();
	if (AmmoData && AmmoData != Item && (AmmoData == Shells || AmmoData == HeavyAmmo))
	{
		if (!((UFortWeaponItemDefinition*)Item)->WeaponStatHandle.DataTable || !((UFortWeaponItemDefinition*)Item)->WeaponStatHandle.RowName.ComparisonIndex)
			return;

		auto DataTable = ((UFortWeaponItemDefinition*)Item)->WeaponStatHandle.DataTable;
		FName Name = ((UFortWeaponItemDefinition*)Item)->WeaponStatHandle.RowName;

		TMap<FName, FFortRangedWeaponStats*>& WeaponStatTable = *(TMap<FName, FFortRangedWeaponStats*>*)(int64(DataTable) + 0x30);

		for (auto& Pair : WeaponStatTable)
		{
			if (Pair.Key().ComparisonIndex == Name.ComparisonIndex)
			{
				Pair.Value()->KnockbackMagnitude = 0;
				Pair.Value()->KnockbackZAngle = 0;
				Pair.Value()->LongRangeKnockbackMagnitude = 0;
				Pair.Value()->MidRangeKnockbackMagnitude = 0;
				break;
			}
		}
	}
}

void Inventory::RemoveItem(AFortPlayerControllerAthena* PC, UFortItemDefinition* Def, int Count, FGuid ItemGuid)
{
	if (!PC || !Def)
		return;
	for (size_t i = 0; i < PC->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
	{
		auto& Item = PC->WorldInventory->Inventory.ReplicatedEntries[i];
		bool IsSameGuid = true;
		if (UKismetGuidLibrary::IsValid_Guid(ItemGuid) && Item.ItemGuid != ItemGuid)
			IsSameGuid = false;
		if (Item.ItemDefinition == Def && IsSameGuid)
		{
			Item.Count -= Count;
			if (Item.Count <= 0)
			{
				for (size_t j = 0; j < PC->WorldInventory->Inventory.ItemInstances.Num(); j++)
				{
					if (!PC->WorldInventory->Inventory.ItemInstances[j])
						continue;
					if (PC->WorldInventory->Inventory.ItemInstances[j]->ItemEntry.ItemGuid == Item.ItemGuid)
					{
						PC->WorldInventory->Inventory.ItemInstances.Remove(j);
						break;
					}
				}
				PC->WorldInventory->Inventory.ReplicatedEntries.Remove(i);
				PC->WorldInventory->Inventory.MarkArrayDirty();
				PC->WorldInventory->HandleInventoryLocalUpdate();
				return;
			}
			PC->WorldInventory->Inventory.MarkItemDirty(Item);
			UpdateItem(PC, Item);
			PC->WorldInventory->HandleInventoryLocalUpdate();
			break;
		}
	}
}

bool Inventory::CheckAndStack(AFortPlayerControllerAthena* PC, UFortItemDefinition* Def, int Count)
{
	if (!PC || !PC->WorldInventory || !Def)
		return true;
	auto MaxStackSize = UFortScalableFloatUtils::GetValueAtLevel(Def->MaxStackSize, 0);
	for (auto& Item : PC->WorldInventory->Inventory.ReplicatedEntries)
	{
		if (Item.ItemDefinition == Def)
		{
			Item.Count += Count;
			if (Item.Count > MaxStackSize)
			{
				if (Def->bAllowMultipleStacks)
				{
					if (GetUsedSlots(PC) < 5)
					{
						GiveItem(PC, Def, Item.Count - MaxStackSize);
						Item.Count = MaxStackSize;
						PC->WorldInventory->Inventory.MarkItemDirty(Item);
						UpdateItem(PC, Item);
						PC->WorldInventory->HandleInventoryLocalUpdate();
						return true;
					}
					else
					{
						Item.Count = MaxStackSize;
						return false;
					}
				}
				else
				{
					int ogCount = Item.Count;
					Item.Count = MaxStackSize;
					PC->WorldInventory->Inventory.MarkItemDirty(Item);
					UpdateItem(PC, Item);
					PC->WorldInventory->HandleInventoryLocalUpdate();

					SpawnPickup(PC->Pawn->K2_GetActorLocation(), Def, EFortPickupSourceTypeFlag::Player, ogCount - MaxStackSize, 0, EFortPickupSpawnSource::Unset, PC->MyFortPawn);

					return true;
				}
			}
			PC->WorldInventory->Inventory.MarkItemDirty(Item);
			UpdateItem(PC, Item);
			PC->WorldInventory->HandleInventoryLocalUpdate();
			return true;
		}
	}
	return false;
}

void Inventory::ServerHandlePickup(AFortPlayerPawnAthena* Pawn, AFortPickup* Pickup, FFortPickupRequestInfo Info)
{

}

void Inventory::DestroyPickup(AFortPickup* Pickup)
{
	if (!Pickup || !Pickup->bPickedUp || !Pickup->PickupLocationData.PickupTarget.Get())
		return;
	//printf("DestroyPickup called\n");
	auto Pawn = (AFortPlayerPawnAthena*)Pickup->PickupLocationData.PickupTarget.Get();
	auto PC = (AFortPlayerControllerAthena*)Pawn->Controller;
	
	if (!PC)
		return;

	if (CheckAndStack(PC, Pickup->PrimaryPickupItemEntry.ItemDefinition, Pickup->PrimaryPickupItemEntry.Count))
		return;

	int UsedSlots = IsPrimary(Pickup->PrimaryPickupItemEntry.ItemDefinition) ? GetUsedSlots(PC) : 0;

	//printf("UsedSlots: %d\n", UsedSlots);

	if (UsedSlots >= 5)
	{
		FFortItemEntry* SwapEntry = nullptr;
		auto SwapGuid = SwapGuids.contains(PC) ? SwapGuids[PC] : FGuid();
		for (auto& Entry : PC->WorldInventory->Inventory.ReplicatedEntries)
		{
			if (Entry.ItemGuid == SwapGuid)
			{
				SwapEntry = &Entry;
				break;
			}
		}
		if (!SwapEntry || SwapEntry->ItemDefinition == PC->CosmeticLoadoutPC.Pickaxe->WeaponDefinition)
		{
			SpawnPickup(Pawn->K2_GetActorLocation(), Pickup->PrimaryPickupItemEntry.ItemDefinition, EFortPickupSourceTypeFlag::Player, Pickup->PrimaryPickupItemEntry.Count, Pickup->PrimaryPickupItemEntry.LoadedAmmo, EFortPickupSpawnSource::Unset, PC->MyFortPawn);
			return;
		}
		SpawnPickup(Pawn->K2_GetActorLocation(), SwapEntry->ItemDefinition, EFortPickupSourceTypeFlag::Player, SwapEntry->Count, SwapEntry->LoadedAmmo, EFortPickupSpawnSource::Unset, PC->MyFortPawn);
		RemoveItem(PC, SwapEntry->ItemDefinition, SwapEntry->Count);
	}

	GiveItem(PC, Pickup->PrimaryPickupItemEntry.ItemDefinition, Pickup->PrimaryPickupItemEntry.Count, Pickup->PrimaryPickupItemEntry.LoadedAmmo, UFortScalableFloatUtils::GetValueAtLevel(Pickup->PrimaryPickupItemEntry.ItemDefinition->MaxStackSize, 0) > 1);

	return;
}

FFortItemEntry& Inventory::GetItemEntry(AFortPlayerControllerAthena* PlayerController, FGuid ItemGuid)
{
	if (PlayerController && PlayerController->WorldInventory)
	{
		for (auto& ReplicatedEntry : PlayerController->WorldInventory->Inventory.ReplicatedEntries)
		{
			if (ReplicatedEntry.ItemGuid == ItemGuid)
			{
				return ReplicatedEntry;
			}
		}
	}
}

__int64 Inventory::RemoveItemHook(IFortInventoryOwnerInterface* Interface, FGuid* ItemGuid, int Count, bool bForceRemoveFromQuickBars, bool bForceRemoval)
{
	auto PC = (AFortPlayerControllerAthena*)(__int64(Interface) - 0x710);
	if (!PC || !PC->WorldInventory)
	{
		//printf("wtf\n");
		return 0;
	}

	for (auto& Entry : PC->WorldInventory->Inventory.ReplicatedEntries)
	{
		if (Entry.ItemGuid == *ItemGuid)
		{
			RemoveItem(PC, Entry.ItemDefinition, Count, Entry.ItemGuid);
			break;
		}
	}

	if (PC->MyFortPawn && PC->MyFortPawn->CurrentWeapon)
	{
		for (auto& Entry : PC->WorldInventory->Inventory.ReplicatedEntries)
		{
			if (Entry.ItemGuid == PC->MyFortPawn->CurrentWeapon->ItemEntryGuid)
			{
				Entry.LoadedAmmo = PC->MyFortPawn->CurrentWeapon->AmmoCount;
				UpdateItem(PC, Entry);
				PC->WorldInventory->Inventory.MarkItemDirty(Entry);
				PC->WorldInventory->HandleInventoryLocalUpdate();
				break;
			}
		}
	}

	return 1;
}

int Inventory::GetClipSize(UFortItemDefinition* Def)
{
	auto WeaponDef = (UFortWeaponItemDefinition*)Def;
	if (!Def->IsA(UFortWeaponItemDefinition::StaticClass()))
		return 0;

	if (!WeaponDef->WeaponStatHandle.DataTable || !WeaponDef->WeaponStatHandle.RowName.ComparisonIndex)
		return 0;

	auto DataTable = WeaponDef->WeaponStatHandle.DataTable;
	FName Name = WeaponDef->WeaponStatHandle.RowName;

	TMap<FName, FFortRangedWeaponStats*>& WeaponStatTable = *(TMap<FName, FFortRangedWeaponStats*>*)(int64(DataTable) + 0x30);

	for (auto& Pair : WeaponStatTable)
	{
		if (Pair.Key().ComparisonIndex == Name.ComparisonIndex)
		{
			return Pair.Value()->ClipSize;
		}
	}

	return 0;
}

AFortPickupAthena* Inventory::SpawnPickup(FVector Loc, FFortItemEntry& Entry)
{
	auto Ret = SpawnActor<AFortPickupAthena>(Loc);
	Ret->PrimaryPickupItemEntry.Count = Entry.Count;
	Ret->PrimaryPickupItemEntry.ItemDefinition = Entry.ItemDefinition;
	Ret->PrimaryPickupItemEntry.LoadedAmmo = Entry.LoadedAmmo;
	Ret->OnRep_PrimaryPickupItemEntry();
	Ret->TossPickup(Loc, nullptr, -1, true, false, EFortPickupSourceTypeFlag::Player, EFortPickupSpawnSource::PlayerElimination);
	auto TimeSeconds = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
	Ret->SetDespawnTime(TimeSeconds + 30);
	Ret->DespawnTime = TimeSeconds + 30;
	Pickups.push_back(Ret);
	return Ret;
}