#pragma once

#include "dusk/mod_api.h"

namespace dusk {
struct LoadedMod;
}

namespace dusk::modding {

DuskActorTypeId DuskModActors_Register(LoadedMod* owner, const DuskModActorDesc* desc);
DuskProcId DuskModActors_Spawn(const DuskModActorSpawnInfo* info);
DuskActorHandle DuskModActors_FromId(DuskProcId proc_id);
const char* DuskModActors_GetDisplayName(DuskActorHandle actor);
DuskActorTypeId DuskModActors_GetTypeId(DuskActorHandle actor);
void* DuskModActors_GetGameActor(DuskActorHandle actor);
void* DuskModActors_GetState(DuskActorHandle actor);
int DuskModActors_Destroy(DuskActorHandle actor);
int DuskModActors_DestroyById(DuskProcId proc_id);
void DuskModActors_UnregisterMod(LoadedMod* owner);
void DuskModActors_Reset();

}  // namespace dusk::modding
