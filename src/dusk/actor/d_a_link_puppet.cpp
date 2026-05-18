/**
 * d_a_link_puppet.cpp
 * Network Link Puppet
 */

#include "d/dolzel_rel.h" // IWYU pragma: keep

#include "dusk/actor/d_a_link_puppet.h"
#include "f_pc/f_pc_name.h"

void daLinkPuppet_c::initBaseMtx() {
    setBaseMtx();
}

void daLinkPuppet_c::setBaseMtx() {
    mDoMtx_stack_c::transS(current.pos);
    MTXCopy(mDoMtx_stack_c::now, mBgMtx);
    MTXCopy(mDoMtx_stack_c::now, mpModel->mBaseTransformMtx);
}

static int daLinkPuppet_Draw(daLinkPuppet_c* i_this) {
    return static_cast<dBgS_MoveBgActor*>(i_this)->MoveBGDraw();
}

static int daLinkPuppet_Execute(daLinkPuppet_c* i_this) {
    return static_cast<dBgS_MoveBgActor*>(i_this)->MoveBGExecute();
}

static int daLinkPuppet_IsDelete(daLinkPuppet_c* i_this) {
    return 1;
}

static int daLinkPuppet_Delete(daLinkPuppet_c* i_this) {
    static_cast<dBgS_MoveBgActor*>(i_this)->MoveBGDelete();
    return 1;
}

static char* l_arcName = "M_IzmGate";

int daLinkPuppet_c::create() {
    fopAcM_ct(this, daLinkPuppet_c);
    int phase = dComIfG_resLoad(&mPhaseReq, l_arcName);
    if (phase == cPhs_COMPLEATE_e) {
        int objectName = dComIfG_getObjctResName2Index(l_arcName, "M_IzumiGate_b.dzb");
        phase = MoveBGCreate(l_arcName, objectName, dBgS_MoveBGProc_TypicalRotY, 0x4000, NULL);
        if (phase == cPhs_ERROR_e) {
            return phase;
        }
        fopAcM_SetMtx(this, mBgMtx);
    }
    return phase;
}

static int daLinkPuppet_Create(fopAc_ac_c* i_this) {
    return static_cast<daLinkPuppet_c*>(i_this)->create();
}

int daLinkPuppet_c::CreateHeap() {
    J3DModelData* modelData = (J3DModelData*)dComIfG_getObjectRes(l_arcName, "M_IzumiGate_b.bmd");
    mpModel = mDoExt_J3DModel__create(modelData, 0x80000, 0x11000084);
    return mpModel != NULL ? 1 : 0;
}

int daLinkPuppet_c::Create() {
    initBaseMtx();
    return cPhs_COMPLEATE_e;
}

int daLinkPuppet_c::Execute(Mtx** i_mtx) {
    *i_mtx = &mBgMtx;
    setBaseMtx();
    return 1;
}

int daLinkPuppet_c::Draw() {
    g_env_light.settingTevStruct(16, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(mpModel, &tevStr);
    dComIfGd_setListBG();
    mDoExt_modelUpdateDL(mpModel);
    dComIfGd_setList();
    return 1;
}

int daLinkPuppet_c::Delete() {
    dComIfG_resDelete(&mPhaseReq, l_arcName);
    return 1;
}

static actor_method_class l_LinkPuppet_Method = {
    (process_method_func)daLinkPuppet_Create,  (process_method_func)daLinkPuppet_Delete,
    (process_method_func)daLinkPuppet_Execute, (process_method_func)daLinkPuppet_IsDelete,
    (process_method_func)daLinkPuppet_Draw,
};

actor_process_profile_definition g_profile_LINKPUPPET = {
    /* Layer ID     */ fpcLy_CURRENT_e,
    /* List ID      */ 7,
    /* List Prio    */ fpcPi_CURRENT_e,
    /* Proc Name    */ fpcNm_LINKPUPPET_e,
    /* Proc SubMtd  */ &g_fpcLf_Method.base,
    /* Size         */ sizeof(daLinkPuppet_c),
    /* Size Other   */ 0,
    /* Parameters   */ 0,
    /* Leaf SubMtd  */ &g_fopAc_Method.base,
    /* Draw Prio    */ fpcDwPi_LINKPUPPET_e,
    /* Actor SubMtd */ &l_LinkPuppet_Method,
    /* Status       */ fopAcStts_UNK_0x40000_e,
    /* Group        */ fopAc_ACTOR_e,
    /* Cull Type    */ fopAc_CULLBOX_CUSTOM_e,
};
