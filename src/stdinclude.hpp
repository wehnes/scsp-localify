#pragma once

#define NOMINMAX

#include <Windows.h>
#include <shlobj.h>

#include <cinttypes>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <thread>
#include <variant>

#include <exception>
#include <vector>
#include <regex>

#include <MinHook.h>

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

#include "il2cpp/il2cpp_symbols.hpp"

#include <nlohmann/json.hpp>
#include <cpprest/uri.h>
#include <cpprest/http_listener.h>
#include <cpprest/http_client.h>
#include <string>

#include "local/local.hpp"
#include "camera/camera.hpp"


class CharaParam_t {
public:
	CharaParam_t(float height, float bust, float head, float arm, float hand) : 
		height(height), bust(bust), head(head), arm(arm), hand(hand) {
		objPtr = NULL;
		updateInitParam();
	}

	CharaParam_t(float height, float bust, float head, float arm, float hand, void* objPtr) :
		height(height), bust(bust), head(head), arm(arm), hand(hand), objPtr(objPtr) {
		updateInitParam();
	}

	void UpdateParam(float* height, float* bust, float* head, float* arm, float* hand) const {
		*height = this->height;
		*bust = this->bust;
		*head = this->head;
		*arm = this->arm;
		*hand = this->hand;
	}

	void SetObjPtr(void* ptr) {
		objPtr = ptr;
	}

	bool checkObjAlive() {
		if (!objPtr) return false;
		static auto Object_IsNativeObjectAlive = reinterpret_cast<bool(*)(void*)>(
			il2cpp_symbols::get_method_pointer("UnityEngine.CoreModule.dll", "UnityEngine",
				"Object", "IsNativeObjectAlive", 1)
			);
		const auto ret = Object_IsNativeObjectAlive(objPtr);
		if (!ret) objPtr = NULL;
		return ret;
	}

	void* getObjPtr() {
		checkObjAlive();
		return objPtr;
	}

	void Reset() {
		height = init_height;
		bust = init_bust;
		head = init_head;
		arm = init_arm;
		hand = init_hand;
	}

	void Apply();
	void ApplyOnMainThread();

	float height;
	float bust;
	float head;
	float arm;
	float hand;
	bool gui_real_time_apply = false;
private:
	void updateInitParam() {
		init_height = height;
		init_bust = bust;
		init_head = head;
		init_arm = arm;
		init_hand = hand;
	}

	void* objPtr;
	float init_height;
	float init_bust;
	float init_head;
	float init_arm;
	float init_hand;
};


extern std::function<void()> g_reload_all_data;
extern bool g_enable_plugin;
extern int g_max_fps;
extern int g_vsync_count;
extern bool g_enable_console;
extern bool g_auto_dump_all_json;
extern bool g_dump_untrans_lyrics;
extern bool g_dump_untrans_unlocal;
extern std::string g_custom_font_path;
extern std::filesystem::path g_localify_base;
extern std::list<std::string> g_extra_assetbundle_paths;
extern char hotKey;
extern bool g_enable_free_camera;
extern bool g_block_out_of_focus;
extern float g_free_camera_mouse_speed;
extern bool g_allow_use_tryon_costume;
extern bool g_allow_same_idol;
extern bool g_unlock_all_dress;
extern bool g_unlock_all_headwear;
extern bool g_enable_chara_param_edit;
extern float g_font_size_offset;
