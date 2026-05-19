#include "dusk/mod_actor.hpp"

#include "SSystem/SComponent/c_malloc.h"
#include "SSystem/SComponent/c_phase.h"
#include "dusk/logging.h"
#include "dusk/mod_loader.hpp"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_layer.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {

struct RegisteredModActor {
    DuskModActorDesc desc{};
    dusk::LoadedMod* owner = nullptr;
    std::string ownerId;
    std::string name;
    bool active = false;
};

struct DuskModActorAppend {
    fopAcM_prm_class base;
    u32 magic;
    DuskActorTypeId typeId;
    u32 spawnParams;
};

struct DuskModActorProcess : public fopAc_ac_c {
    DuskActorTypeId typeId;
    u32 spawnParams;
    void* state;
};

static_assert(sizeof(DuskModActorProcess) >= sizeof(fopAc_ac_c));

static constexpr u32 kModActorAppendMagic = 0x44534143; // DSAC
static std::vector<RegisteredModActor> sActorTypes;

static bool Is_Power_Of_Two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static size_t Normalize_Alignment(size_t alignment) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    if (!Is_Power_Of_Two(alignment)) {
        return 0;
    }

    return alignment;
}

static void* Allocate_State(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    alignment = Normalize_Alignment(alignment);
    if (alignment == 0) {
        return nullptr;
    }

#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, aligned_size);
#endif
}

static void Free_State(void* state) {
    if (state == nullptr) {
        return;
    }

#if defined(_WIN32)
    _aligned_free(state);
#else
    std::free(state);
#endif
}

static RegisteredModActor* Find_Type(DuskActorTypeId type_id) {
    if (type_id == 0 || type_id > sActorTypes.size()) {
        return nullptr;
    }

    RegisteredModActor& type = sActorTypes[type_id - 1];
    if (!type.active) {
        return nullptr;
    }

    return &type;
}

static RegisteredModActor* Find_Type_Any_State(DuskActorTypeId type_id) {
    if (type_id == 0 || type_id > sActorTypes.size()) {
        return nullptr;
    }

    return &sActorTypes[type_id - 1];
}

static DuskModActorProcess* To_Mod_Actor(DuskActorHandle handle) {
    if (handle == nullptr) {
        return nullptr;
    }

    fopAc_ac_c* actor = static_cast<fopAc_ac_c*>(handle);
    if (fpcM_GetProfName(actor) != fpcNm_DUSK_MOD_ACTOR_e) {
        return nullptr;
    }

    return static_cast<DuskModActorProcess*>(actor);
}

static s32 DuskModActor_CreateHeapCallback(fopAc_ac_c* game_actor) {
    DuskModActorProcess* actor = static_cast<DuskModActorProcess*>(game_actor);
    RegisteredModActor* type = Find_Type(actor->typeId);
    if (type == nullptr || type->desc.create == nullptr) {
        return FALSE;
    }

    return type->desc.create(actor, actor->state, type->desc.userdata) != 0;
}

static DuskModActorAppend* Create_Append(const DuskModActorSpawnInfo* info) {
    DuskModActorAppend* append =
        static_cast<DuskModActorAppend*>(cMl::memalignB(-4, sizeof(DuskModActorAppend)));
    if (append == nullptr) {
        return nullptr;
    }

    std::memset(append, 0, sizeof(*append));
    cXyz position(info->pos_x, info->pos_y, info->pos_z);

    append->base.base.setID = 0xFFFF;
    append->base.base.parameters = info->params;
    append->base.base.position = position;
    append->base.base.angle.set(info->angle_x, info->angle_y, info->angle_z);
    append->base.scale.x = static_cast<u8>(std::clamp(info->scale_x, 0.0f, 25.5f) * 10.0f);
    append->base.scale.y = static_cast<u8>(std::clamp(info->scale_y, 0.0f, 25.5f) * 10.0f);
    append->base.scale.z = static_cast<u8>(std::clamp(info->scale_z, 0.0f, 25.5f) * 10.0f);
    append->base.parent_id = fpcM_ERROR_PROCESS_ID_e;
    append->base.argument = info->argument;
    append->base.room_no = static_cast<s8>(std::clamp(info->room_no, -128, 127));
    append->magic = kModActorAppendMagic;
    append->typeId = info->type_id;
    append->spawnParams = info->params;
    return append;
}

static int DuskModActor_Create(DuskModActorProcess* actor) {
    fopAcM_ct(actor, DuskModActorProcess);

    DuskModActorAppend* append = static_cast<DuskModActorAppend*>(fpcM_GetAppend(actor));
    if (append == nullptr || append->magic != kModActorAppendMagic) {
        DuskLog.error("DuskModActor_Create: missing mod actor append data");
        return cPhs_ERROR_e;
    }

    RegisteredModActor* type = Find_Type(append->typeId);
    if (type == nullptr) {
        DuskLog.error("DuskModActor_Create: unknown actor type {}", append->typeId);
        return cPhs_ERROR_e;
    }

    actor->typeId = append->typeId;
    actor->spawnParams = append->spawnParams;
    actor->state = Allocate_State(type->desc.state_size, type->desc.state_align);
    if (type->desc.state_size != 0 && actor->state == nullptr) {
        DuskLog.error("DuskModActor_Create: failed to allocate state for '{}'", type->name);
        return cPhs_ERROR_e;
    }

    if (actor->state != nullptr) {
        std::memset(actor->state, 0, type->desc.state_size);
    }

    bool create_ok = true;
    if (type->desc.create != nullptr) {
        if (type->desc.heap_size != 0) {
            create_ok = fopAcM_entrySolidHeap(actor, DuskModActor_CreateHeapCallback, type->desc.heap_size);
        }
        else {
            create_ok = type->desc.create(actor, actor->state, type->desc.userdata) != 0;
        }
    }

    if (!create_ok) {
        Free_State(actor->state);
        actor->state = nullptr;
        return cPhs_ERROR_e;
    }

    return cPhs_COMPLEATE_e;
}

static int DuskModActor_DestroyCallback(DuskModActorProcess* actor) {
    RegisteredModActor* type = Find_Type(actor->typeId);
    if (type != nullptr && type->desc.destroy != nullptr) {
        type->desc.destroy(actor, actor->state, type->desc.userdata);
    }

    Free_State(actor->state);
    actor->state = nullptr;
    return TRUE;
}

static int DuskModActor_Execute(DuskModActorProcess* actor) {
    RegisteredModActor* type = Find_Type(actor->typeId);
    if (type == nullptr || type->desc.execute == nullptr) {
        return TRUE;
    }

    return type->desc.execute(actor, actor->state, type->desc.userdata) != 0;
}

static int DuskModActor_Draw(DuskModActorProcess* actor) {
    RegisteredModActor* type = Find_Type(actor->typeId);
    if (type == nullptr || type->desc.draw == nullptr) {
        return TRUE;
    }

    return type->desc.draw(actor, actor->state, type->desc.userdata) != 0;
}

static int DuskModActor_IsDelete(DuskModActorProcess* actor) {
    RegisteredModActor* type = Find_Type(actor->typeId);
    if (type == nullptr || type->desc.is_delete == nullptr) {
        return TRUE;
    }

    return type->desc.is_delete(actor, actor->state, type->desc.userdata) != 0;
}

static actor_method_class sDuskModActorMethods = {
    (process_method_func)DuskModActor_Create,
    (process_method_func)DuskModActor_DestroyCallback,
    (process_method_func)DuskModActor_Execute,
    (process_method_func)DuskModActor_IsDelete,
    (process_method_func)DuskModActor_Draw,
};

}  // namespace

actor_process_profile_definition g_profile_DUSK_MOD_ACTOR = {
    /* Layer ID     */ fpcLy_CURRENT_e,
    /* List ID      */ 9,
    /* List Prio    */ fpcPi_CURRENT_e,
    /* Proc Name    */ fpcNm_DUSK_MOD_ACTOR_e,
    /* Proc SubMtd  */ &g_fpcLf_Method.base,
    /* Size         */ sizeof(DuskModActorProcess),
    /* Size Other   */ 0,
    /* Parameters   */ 0,
    /* Leaf SubMtd  */ &g_fopAc_Method.base,
    /* Draw Prio    */ fpcDwPi_ARROW_e,
    /* Actor SubMtd */ &sDuskModActorMethods,
    /* Status       */ fopAcStts_UNK_0x40000_e,
    /* Group        */ fopAc_ACTOR_e,
    /* Cull Type    */ fopAc_CULLBOX_CUSTOM_e,
};

namespace dusk::modding {

DuskActorTypeId DuskModActors_Register(LoadedMod* owner, const DuskModActorDesc* desc) {
    if (owner == nullptr || desc == nullptr) {
        return 0;
    }

    if (desc->struct_size < offsetof(DuskModActorDesc, userdata) + sizeof(desc->userdata)) {
        DuskLog.error("[{}] actor_register_type: bad descriptor size", owner->metadata.id);
        return 0;
    }

    if (desc->name == nullptr || desc->name[0] == '\0') {
        DuskLog.error("[{}] actor_register_type: missing actor name", owner->metadata.id);
        return 0;
    }

    if (desc->state_size != 0 && Normalize_Alignment(desc->state_align) == 0) {
        DuskLog.error("[{}] actor_register_type: '{}' has invalid state alignment",
            owner->metadata.id, desc->name);
        return 0;
    }

    RegisteredModActor type{};
    size_t copy_size = std::min<size_t>(desc->struct_size, sizeof(DuskModActorDesc));
    std::memcpy(&type.desc, desc, copy_size);
    type.owner = owner;
    type.ownerId = owner->metadata.id;
    type.name = desc->name;
    type.active = true;

    sActorTypes.push_back(type);
    DuskActorTypeId type_id = static_cast<DuskActorTypeId>(sActorTypes.size());
    DuskLog.info("[{}] registered mod actor '{}' as type {}", owner->metadata.id, type.name, type_id);
    return type_id;
}

DuskProcId DuskModActors_Spawn(const DuskModActorSpawnInfo* info) {
    if (info == nullptr) {
        return fpcM_ERROR_PROCESS_ID_e;
    }

    if (info->struct_size < offsetof(DuskModActorSpawnInfo, argument) + sizeof(info->argument)) {
        return fpcM_ERROR_PROCESS_ID_e;
    }

    if (Find_Type(info->type_id) == nullptr) {
        return fpcM_ERROR_PROCESS_ID_e;
    }

    DuskModActorSpawnInfo normalized = *info;
    if (normalized.scale_x == 0.0f) {
        normalized.scale_x = 1.0f;
    }
    if (normalized.scale_y == 0.0f) {
        normalized.scale_y = 1.0f;
    }
    if (normalized.scale_z == 0.0f) {
        normalized.scale_z = 1.0f;
    }

    DuskModActorAppend* append = Create_Append(&normalized);
    if (append == nullptr) {
        return fpcM_ERROR_PROCESS_ID_e;
    }

    layer_class* saved_layer = fpcLy_CurrentLayer();
    base_process_class* play_scene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (play_scene != nullptr) {
        fpcLy_SetCurrentLayer(&reinterpret_cast<process_node_class*>(play_scene)->layer);
    }

    fpc_ProcID result = fpcM_Create(fpcNm_DUSK_MOD_ACTOR_e, nullptr, append);
    fpcLy_SetCurrentLayer(saved_layer);
    if (result == fpcM_ERROR_PROCESS_ID_e) {
        cMl::free(append);
    }
    return result;
}

DuskActorHandle DuskModActors_FromId(DuskProcId proc_id) {
    void* actor = fopAcM_SearchByID(proc_id);
    if (actor == nullptr) {
        return nullptr;
    }

    return To_Mod_Actor(actor);
}

const char* DuskModActors_GetDisplayName(DuskActorHandle actor) {
    DuskModActorProcess* mod_actor = To_Mod_Actor(actor);
    if (mod_actor == nullptr) {
        return nullptr;
    }

    RegisteredModActor* type = Find_Type_Any_State(mod_actor->typeId);
    if (type == nullptr) {
        return "DUSK_MOD_ACTOR:<unknown>";
    }

    static thread_local std::string sDisplayName;
    if (type->active) {
        sDisplayName = "DUSK_MOD_ACTOR:";
    }
    else {
        sDisplayName = "DUSK_MOD_ACTOR:<unloaded>:";
    }

    sDisplayName += type->ownerId.empty() ? "unknown_mod" : type->ownerId;
    sDisplayName += ".";
    sDisplayName += type->name.empty() ? "UnknownActor" : type->name;
    sDisplayName += " #";
    sDisplayName += std::to_string(mod_actor->typeId);
    return sDisplayName.c_str();
}

DuskActorTypeId DuskModActors_GetTypeId(DuskActorHandle actor) {
    DuskModActorProcess* mod_actor = To_Mod_Actor(actor);
    if (mod_actor == nullptr) {
        return 0;
    }

    return mod_actor->typeId;
}

void* DuskModActors_GetGameActor(DuskActorHandle actor) {
    return To_Mod_Actor(actor);
}

void* DuskModActors_GetState(DuskActorHandle actor) {
    DuskModActorProcess* mod_actor = To_Mod_Actor(actor);
    if (mod_actor == nullptr) {
        return nullptr;
    }

    return mod_actor->state;
}

int DuskModActors_Destroy(DuskActorHandle actor) {
    DuskModActorProcess* mod_actor = To_Mod_Actor(actor);
    if (mod_actor == nullptr) {
        return FALSE;
    }

    return fopAcM_delete(mod_actor);
}

int DuskModActors_DestroyById(DuskProcId proc_id) {
    DuskActorHandle actor = DuskModActors_FromId(proc_id);
    if (actor == nullptr) {
        return FALSE;
    }

    return DuskModActors_Destroy(actor);
}

void DuskModActors_UnregisterMod(LoadedMod* owner) {
    if (owner == nullptr) {
        return;
    }

    for (RegisteredModActor& type : sActorTypes) {
        if (type.owner == owner) {
            type.active = false;
            type.owner = nullptr;
        }
    }
}

void DuskModActors_Reset() {
    sActorTypes.clear();
}

}  // namespace dusk::modding
