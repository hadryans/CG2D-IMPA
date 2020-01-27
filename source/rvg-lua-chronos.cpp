#include <chrono>
#include <thread>

#include <lua.h>
#include <lauxlib.h>
#include "rvg-lua.h"

#include "rvg-chronos.h"
#include "rvg-lua-chronos.h"

template <>
int rvg_lua_tostring<rvg::chronos>(lua_State *L) {
    rvg::chronos *time = rvg_lua_check_pointer<rvg::chronos>(L, 1);
    lua_pushfstring(L, "chronos{%f}", time->time());
    return 1;
}

static int resetchronos(lua_State *L) {
    rvg::chronos *time = rvg_lua_check_pointer<rvg::chronos>(L, 1);
    time->reset();
    return 0;
}

static int elapsedchronos(lua_State *L) {
    rvg::chronos *time = rvg_lua_check_pointer<rvg::chronos>(L, 1);
    lua_pushnumber(L, time->elapsed());
    return 1;
}

static int timechronos(lua_State *L) {
    rvg::chronos *time = rvg_lua_check_pointer<rvg::chronos>(L, 1);
    lua_pushnumber(L, time->time());
    return 1;
}

static int sleepchronos(lua_State *L) {
    std::this_thread::sleep_for(
        std::chrono::duration<double>(
            luaL_checknumber(L, 1)));
    return 0;
}

static const luaL_Reg methodschronos[] = {
    {"reset", resetchronos},
    {"time", timechronos},
    {"elapsed", elapsedchronos},
    {NULL, NULL}
};

static int newchronos(lua_State *L) {
    rvg_lua_push<rvg::chronos>(L, rvg::chronos{});
    return 1;
}

static const luaL_Reg modchronos[] = {
    {"chronos", newchronos},
    {"sleep", sleepchronos},
    {NULL, NULL}
};

extern "C"
#ifndef _WIN32
__attribute__((visibility("default")))
#else
__declspec(dllexport)
#endif
int luaopen_chronos(lua_State *L) {
    lua_newtable(L); // mod
    rvg_lua_init(L); // mod ctxtab
    rvg_lua_createtype<rvg::chronos>(L, "chronos", -1); // mod ctxtab
    rvg_lua_setmethods<rvg::chronos>(L, methodschronos, 0, -1); // mod ctxtab
    rvg_lua_setfuncs(L, modchronos, 1); // mod
    return 1;
}
