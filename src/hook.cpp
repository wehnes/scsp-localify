#include <stdinclude.hpp>
#include <unordered_set>
#include <ranges>
#include <set>
#include <Psapi.h>
#include <intsafe.h>
#include "scgui/scGUIData.hpp"
#include <scgui/scGUIMain.hpp>
#include <mhotkey.hpp>

//using namespace std;

std::function<void()> g_on_hook_ready;
std::function<void()> g_on_close;
std::function<void()> on_hotKey_0;
bool needPrintStack = false;
std::vector<std::pair<std::pair<int, int>, int>> replaceDressResIds{};
std::map<std::string, CharaParam_t> charaParam{};
CharaParam_t baseParam(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
std::vector<std::function<bool()>> mainThreadTasks{};  // 返回 true，执行后移除列表；返回 false，执行后不移除

template<typename T, typename TF>
void convertPtrType(T* cvtTarget, TF func_ptr) {
	*cvtTarget = reinterpret_cast<T>(func_ptr);
}

#pragma region HOOK_MACRO
#define ADD_HOOK(_name_, _fmt_) \
	auto _name_##_offset = reinterpret_cast<void*>(_name_##_addr); \
 	\
	const auto _name_##stat1 = MH_CreateHook(_name_##_offset, _name_##_hook, &_name_##_orig); \
	const auto _name_##stat2 = MH_EnableHook(_name_##_offset); \
	printf(_fmt_##" (%s, %s)\n", _name_##_offset, MH_StatusToString(_name_##stat1), MH_StatusToString(_name_##stat2)); 
#pragma endregion

bool exd = false;
void* SetResolution_orig;
void* AssembleCharacter_ApplyParam_orig;
Il2CppString* (*environment_get_stacktrace)();


void CharaParam_t::Apply() {
	const auto ApplyParamFunc = reinterpret_cast<void (*)(void*, float, float, float, float, float)>(
		AssembleCharacter_ApplyParam_orig
		);
	auto currObjPtr = getObjPtr();
	if (currObjPtr) {
		ApplyParamFunc(currObjPtr, height + baseParam.height, bust + baseParam.bust,
			head + baseParam.head, arm + baseParam.arm, hand + baseParam.hand);
	}
}

void CharaParam_t::ApplyOnMainThread() {
	mainThreadTasks.push_back([this](){
		this->Apply();
		if (!g_enable_chara_param_edit) return true;
		return !(this->gui_real_time_apply || baseParam.gui_real_time_apply);
		});
}


namespace Utils {
	template <typename KT = void*, typename VT = void*>
	class CSDictEditor {
	public:
		// @param dict: Dictionary instance.
		// @param dictTypeStr: Reflection type. eg: "System.Collections.Generic.Dictionary`2[System.Int32, System.Int32]"
		CSDictEditor(void* dict, const char* dictTypeStr) {
			dic_klass = il2cpp_symbols::get_system_class_from_reflection_type_str(dictTypeStr);
			this->dict = dict;

			get_Item_method = il2cpp_class_get_method_from_name(dic_klass, "get_Item", 1);
			Add_method = il2cpp_class_get_method_from_name(dic_klass, "Add", 2);
			ContainsKey_method = il2cpp_class_get_method_from_name(dic_klass, "ContainsKey", 1);

			dic_get_Item = (dic_get_Item_t)get_Item_method->methodPointer;
			dic_Add = (dic_Add_t)Add_method->methodPointer;
			dic_containsKey = (dic_containsKey_t)ContainsKey_method->methodPointer;
		}

		void Add(KT key, VT value) {
			dic_Add(dict, key, value, Add_method);
		}

		bool ContainsKey(KT key) {
			return dic_containsKey(dict, key, ContainsKey_method);
		}

		VT get_Item(KT key) {
			return dic_get_Item(dict, key, get_Item_method);
		}

		VT operator[] (KT key) {
			return get_Item(key);
		}

		void* dict;
		void* dic_klass;

	private:
		typedef VT(*dic_get_Item_t)(void*, KT, void* mtd);
		typedef VT(*dic_Add_t)(void*, KT, VT, void* mtd);
		typedef VT(*dic_containsKey_t)(void*, KT, void* mtd);

		CSDictEditor();
		MethodInfo* get_Item_method;
		MethodInfo* Add_method;
		MethodInfo* ContainsKey_method;
		dic_get_Item_t dic_get_Item;
		dic_Add_t dic_Add;
		dic_containsKey_t dic_containsKey;
	};

}


namespace
{
	void path_game_assembly();
	void patchNP(HMODULE module);
	bool mh_inited = false;
	void* load_library_w_orig = nullptr;
	bool (*CloseNPGameMon)();

	void reopen_self() {
		int result = MessageBoxW(NULL, L"未检测到目标加载，插件初始化失败。是否重启游戏？", L"提示", MB_OKCANCEL);

		if (result == IDOK)
		{
			STARTUPINFOA startupInfo{ .cb = sizeof(STARTUPINFOA) };
			PROCESS_INFORMATION pi{};
			const auto commandLine = GetCommandLineA();
			if (CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &pi)) {
				// printf("open external plugin: %s (%lu)\n", commandLine, pi.dwProcessId);
				// DWORD dwRetun = 0;
				// WaitForSingleObject(pi.hProcess, INFINITE);
				// GetExitCodeProcess(pi.hProcess, &dwRetun);
				// printf("plugin exit: %d\n", dwRetun);
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
			}
			TerminateProcess(GetCurrentProcess(), 0);
		}
	}

	HMODULE __stdcall load_library_w_hook(const wchar_t* path)
	{
		using namespace std;
		// printf("load library: %ls\n", path);
		if (!exd && (wstring_view(path).find(L"NPGameDLL.dll") != wstring_view::npos)) {
			// printf("fing NP\n");
			// Sleep(10000000000000);
			// printf("end sleep\n");
			auto ret = reinterpret_cast<decltype(LoadLibraryW)*>(load_library_w_orig)(path);
			patchNP(ret);
			if (environment_get_stacktrace) {
				// printf("NP: %ls\n", environment_get_stacktrace()->start_char);
			}
			return ret;
		}
		// else if (path == L"GameAssembly.dll"sv) {
		//	auto ret = reinterpret_cast<decltype(LoadLibraryW)*>(load_library_w_orig)(path);
		//	return ret;
		// }
		else if (path == L"cri_ware_unity.dll"sv) {
			path_game_assembly();
		}

		return reinterpret_cast<decltype(LoadLibraryW)*>(load_library_w_orig)(path);
	}

	void* (*Encoding_get_UTF8)();
	Il2CppString* (*Encoding_GetString)(void* _this, void* bytes);
	bool isInitedEncoding = false;

	void initEncoding() {
		if (isInitedEncoding) return;
		isInitedEncoding = true;
		convertPtrType(&Encoding_get_UTF8, il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.Text", "Encoding", "get_UTF8", 0));
		convertPtrType(&Encoding_GetString, il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.Text", "Encoding", "GetString", 1) - 0x1B0);
		// convertPtrType(&Encoding_GetString, 0x182ABF800);  // F800 = PTR - 0x1B0
		// printf("Encoding_GetString at %p\n", Encoding_GetString);
		/*
		auto Encoding_GetString_addr = il2cpp_symbols::find_method("mscorlib.dll", "System.Text", "Encoding", [](const MethodInfo* mtd) {
			if (mtd->parameters_count == 1) {
				//if (strcmp(mtd->name, "GetString") == 0) {
				//	printf("GetString Type: %x\n", mtd->parameters->parameter_type->type);
				//}
			}
			return false;
			});
			*/
			
	}

	Il2CppString* bytesToIl2cppString(void* bytes) {
		initEncoding();
		auto encodingInstance = Encoding_get_UTF8();
		// return Encoding_GetString(encodingInstance, bytes);
		return Encoding_GetString(encodingInstance, bytes);
	}

	struct TransparentStringHash : std::hash<std::wstring>, std::hash<std::wstring_view>
	{
		using is_transparent = void;
	};

	typedef std::unordered_set<std::wstring, TransparentStringHash, std::equal_to<void>> AssetPathsType;
	std::map<std::string, AssetPathsType> CustomAssetBundleAssetPaths;
	std::unordered_map<std::string, uint32_t> CustomAssetBundleHandleMap{};
	void* (*AssetBundle_LoadFromFile)(Il2CppString* path, UINT32 crc, UINT64 offset);

	void LoadExtraAssetBundle()
	{
		if (g_extra_assetbundle_paths.empty())
		{
			return;
		}
		// CustomAssetBundleHandleMap.clear();
		// CustomAssetBundleAssetPaths.clear();
		// assert(!ExtraAssetBundleHandle && ExtraAssetBundleAssetPaths.empty());

		static auto AssetBundle_GetAllAssetNames = reinterpret_cast<void* (*)(void*)>(
			il2cpp_resolve_icall("UnityEngine.AssetBundle::GetAllAssetNames()")
			);

		for (const auto& i : g_extra_assetbundle_paths) {
			if (CustomAssetBundleHandleMap.contains(i)) continue;

			const auto extraAssetBundle = AssetBundle_LoadFromFile(il2cpp_string_new(i.c_str()), 0, 0);
			if (extraAssetBundle)
			{
				const auto allAssetPaths = AssetBundle_GetAllAssetNames(extraAssetBundle);
				AssetPathsType assetPath{};
				il2cpp_symbols::iterate_IEnumerable<Il2CppString*>(allAssetPaths, [&assetPath](Il2CppString* path)
					{
						// ExtraAssetBundleAssetPaths.emplace(path->start_char);
						// printf("Asset loaded: %ls\n", path->start_char);
						assetPath.emplace(path->start_char);
					});
				CustomAssetBundleAssetPaths.emplace(i, assetPath);
				CustomAssetBundleHandleMap.emplace(i, il2cpp_gchandle_new(extraAssetBundle, false));
			}
			else
			{
				printf("Cannot load asset bundle: %s\n", i.c_str());
			}
		}
	}

	uint32_t GetBundleHandleByAssetName(std::wstring assetName) {
		for (const auto& i : CustomAssetBundleAssetPaths) {
			for (const auto& m : i.second) {
				if (std::equal(m.begin(), m.end(), assetName.begin(), assetName.end(),
					[](wchar_t c1, wchar_t c2) {
						return std::tolower(c1, std::locale()) == std::tolower(c2, std::locale());
					})) {
					return CustomAssetBundleHandleMap.at(i.first);
				}
			}
		}
		return NULL;
	}

	uint32_t GetBundleHandleByAssetName(std::string assetName) {
		return GetBundleHandleByAssetName(utility::conversions::to_string_t(assetName));
	}

	uint32_t ReplaceFontHandle;
	bool (*Object_IsNativeObjectAlive)(void*);
	void* (*AssetBundle_LoadAsset)(void* _this, Il2CppString* name, Il2CppReflectionType* type);
	Il2CppReflectionType* Font_Type;

	void* getReplaceFont() {
		void* replaceFont{};
		if (g_custom_font_path.empty()) return replaceFont;
		const auto& bundleHandle = GetBundleHandleByAssetName(g_custom_font_path);
		if (bundleHandle)
		{
			if (ReplaceFontHandle)
			{
				replaceFont = il2cpp_gchandle_get_target(ReplaceFontHandle);
				// 加载场景时会被 Resources.UnloadUnusedAssets 干掉，且不受 DontDestroyOnLoad 影响，暂且判断是否存活，并在必要的时候重新加载
				// TODO: 考虑挂载到 GameObject 上
				// AssetBundle 不会被干掉
				if (Object_IsNativeObjectAlive(replaceFont))
				{
					return replaceFont;
				}
				else
				{
					il2cpp_gchandle_free(std::exchange(ReplaceFontHandle, 0));
				}
			}

			const auto extraAssetBundle = il2cpp_gchandle_get_target(bundleHandle);
			replaceFont = AssetBundle_LoadAsset(extraAssetBundle, il2cpp_string_new(g_custom_font_path.c_str()), Font_Type);
			if (replaceFont)
			{
				ReplaceFontHandle = il2cpp_gchandle_new(replaceFont, false);
			}
			else
			{
				std::wprintf(L"Cannot load asset font\n");
			}
		}
		else
		{
			std::wprintf(L"Cannot find asset font\n");
		}
		return replaceFont;
	}

	void* DataFile_GetBytes_orig;
	bool (*DataFile_IsKeyExist)(Il2CppString*);

	void set_fps_hook(int value);
	void set_vsync_count_hook(int value);

	std::filesystem::path dumpBasePath("dumps");

	int dumpScenarioDataById(const std::string& sid) {
		int i = 0;
		int notFoundCount = 0;
		int dumpedCount = 0;
		printf("dumping: %s\n", sid.c_str());
		while (true) {
			const auto dumpName = std::format(L"{}_{:02}.json", utility::conversions::to_utf16string(sid), i);
			const auto dumpNameIl = il2cpp_symbols::NewWStr(dumpName);
			if (!DataFile_IsKeyExist(dumpNameIl)) {
				printf("%ls - continue.\n", dumpName.c_str());
				notFoundCount++;
				if (notFoundCount < 5) {
					i++;
					continue;
				}
				else {
					break;
				}
			}
			auto dataBytes = reinterpret_cast<void* (*)(Il2CppString*)>(DataFile_GetBytes_orig)(dumpNameIl);
			auto newStr = bytesToIl2cppString(dataBytes);
			auto writeWstr = std::wstring(newStr->start_char);
			const std::wstring searchStr = L"\r";
			const std::wstring replaceStr = L"\\r";
			size_t pos = writeWstr.find(searchStr);
			while (pos != std::wstring::npos) {
				writeWstr.replace(pos, 1, replaceStr);
				pos = writeWstr.find(searchStr, pos + replaceStr.length());
			}
			if (writeWstr.ends_with(L",]")) {  // 代哥的 Json 就是不一样
				writeWstr.erase(writeWstr.length() - 2, 1);
			}
			const auto dumpLocalFilePath = dumpBasePath / SCLocal::getFilePathByName(dumpName, true, dumpBasePath);
			std::ofstream dumpFile(dumpLocalFilePath, std::ofstream::out);
			dumpFile << utility::conversions::to_utf8string(writeWstr).c_str();
			printf("dump %ls success. (%ls)\n", dumpName.c_str(), dumpLocalFilePath.c_str());
			dumpedCount++;
			i++;
		}
		return dumpedCount;
	}

	void dumpByteArray(const std::wstring& tag, const std::wstring& name, void* bytes) {
		static auto WriteAllBytes = reinterpret_cast<void (*)(Il2CppString*, void*)>(
			il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.IO",
				"File", "WriteAllBytes", 2)
			);

		std::filesystem::path dumpPath = dumpBasePath / tag;
		if (!std::filesystem::is_directory(dumpPath)) {
			std::filesystem::create_directories(dumpPath);
		}
		std::filesystem::path dumpName = dumpPath / name;
		WriteAllBytes(il2cpp_symbols::NewWStr(dumpName.c_str()), bytes);

	}

	std::string localizationDataCache("{}");
	int dumpAllScenarioData() {
		int totalCount = 0;
		const auto titleData = nlohmann::json::parse(localizationDataCache);
		for (auto& it : titleData.items()) {
			const auto& key = it.key();
			const std::string_view keyView(key);
			if (keyView.starts_with("mlADVInfo_Title")) {
				totalCount += dumpScenarioDataById(key.substr(16));
			}
		}
		return totalCount;
	}

	bool guiStarting = false;
	void startSCGUI() {
		if (guiStarting) return;
		guiStarting = true;
		std::thread([]() {
			printf("GUI START\n");
			guimain();
			guiStarting = false;
			printf("GUI END\n");
			}).detach();
	}

	void itLocalizationManagerDic(void* _this) {
		if (!SCGUIData::needExtractText) return;
		SCGUIData::needExtractText = false;

		static auto LocalizationManager_klass = il2cpp_symbols::get_class_from_instance(_this);
		static auto dic_field = il2cpp_class_get_field_from_name(LocalizationManager_klass, "dic");
		auto dic = il2cpp_symbols::read_field(_this, dic_field);

		static auto toJsonStr = reinterpret_cast<Il2CppString * (*)(void*)>(
			il2cpp_symbols::get_method_pointer("Newtonsoft.Json.dll", "Newtonsoft.Json",
				"JsonConvert", "SerializeObject", 1)
			);

		auto jsonStr = toJsonStr(dic);
		printf("dump text...\n");
		std::wstring jsonWStr(jsonStr->start_char);

		if (!std::filesystem::is_directory(dumpBasePath)) {
			std::filesystem::create_directories(dumpBasePath);
		}
		std::ofstream file(dumpBasePath / "localify.json", std::ofstream::out);
		const auto writeStr = utility::conversions::to_utf8string(jsonWStr);
		localizationDataCache = writeStr;
		file << writeStr.c_str();
		file.close();
		printf("write done.\n");

		const auto dumpCount = dumpAllScenarioData();
		printf("Dump finished. Total %d files\n", dumpCount);

		return;
	}

	void updateDicText(void* _this, Il2CppString* category, int id, std::string& newStr) {
		static auto LocalizationManager_klass = il2cpp_symbols::get_class_from_instance(_this);
		static auto dic_field = il2cpp_class_get_field_from_name(LocalizationManager_klass, "dic");
		auto dic = il2cpp_symbols::read_field(_this, dic_field);

		static auto dic_klass = il2cpp_symbols::get_class_from_instance(dic);
		static auto dict_get_Item = reinterpret_cast<void* (*)(void*, void*)>(
			il2cpp_class_get_method_from_name(dic_klass, "get_Item", 1)->methodPointer
			);

		auto categoryDict = dict_get_Item(dic, category);
		static auto cdic_klass = il2cpp_symbols::get_class_from_instance(categoryDict);
		static auto dict_set_Item = reinterpret_cast<void* (*)(void*, int, void*)>(
			il2cpp_class_get_method_from_name(cdic_klass, "set_Item", 2)->methodPointer
			);
		dict_set_Item(categoryDict, id, il2cpp_string_new(newStr.c_str()));

	}

	void* LocalizationManager_GetTextOrNull_orig;
	Il2CppString* LocalizationManager_GetTextOrNull_hook(void* _this, Il2CppString* category, int id) {
		if (g_max_fps != -1) set_fps_hook(g_max_fps);
		if (g_vsync_count != -1) set_vsync_count_hook(g_vsync_count);
		itLocalizationManagerDic(_this);
		std::string resultText = "";
		if (SCLocal::getLocalifyText(utility::conversions::to_utf8string(category->start_char), id, &resultText)) {
			//updateDicText(_this, category, id, resultText);
			return il2cpp_string_new(resultText.c_str());
		}
		// GG category: mlPublicText_GameGuard, id: 1
		return reinterpret_cast<decltype(LocalizationManager_GetTextOrNull_hook)*>(LocalizationManager_GetTextOrNull_orig)(_this, category, id);
	}

	void* get_NeedsLocalization_orig;
	bool get_NeedsLocalization_hook(void* _this) {
		auto ret = reinterpret_cast<decltype(get_NeedsLocalization_hook)*>(get_NeedsLocalization_orig)(_this);
		//printf("get_NeedsLocalization: %d\n", ret);
		return ret;
	}

	void* LiveMVView_UpdateLyrics_orig;
	void LiveMVView_UpdateLyrics_hook(void* _this, Il2CppString* text) {
		const std::wstring origWstr(text->start_char);
		const auto newText = SCLocal::getLyricsTrans(origWstr);
		return reinterpret_cast<decltype(LiveMVView_UpdateLyrics_hook)*>(LiveMVView_UpdateLyrics_orig)(_this, il2cpp_string_new(newText.c_str()));
	}

	std::pair<std::wstring, std::string> lastLrc{};
	void* TimelineController_SetLyric_orig;
	void TimelineController_SetLyric_hook(void* _this, Il2CppString* text) {
		const std::wstring origWstr(text->start_char);
		std::string newText = "";
		if (!origWstr.empty()) {
			if (lastLrc.first.compare(origWstr) != 0) {
				newText = SCLocal::getLyricsTrans(origWstr);
				lastLrc.first = origWstr;
				lastLrc.second = newText;
			}
			else {
				newText = lastLrc.second;
			}
		}
		return reinterpret_cast<decltype(TimelineController_SetLyric_hook)*>(TimelineController_SetLyric_orig)(_this, il2cpp_string_new(newText.c_str()));
	}

	void* TMP_Text_set_text_orig;
	void TMP_Text_set_text_hook(void* _this, Il2CppString* value) {
		if (needPrintStack) {
			//wprintf(L"TMP_Text_set_text: %ls\n", value->start_char);
			//printf("%ls\n\n", environment_get_stacktrace()->start_char);
		}
		//if(value) value = il2cpp_symbols::NewWStr(std::format(L"(h){}", std::wstring(value->start_char)));
		reinterpret_cast<decltype(TMP_Text_set_text_hook)*>(TMP_Text_set_text_orig)(
			_this, value
			);
	}

	void* (*TMP_FontAsset_CreateFontAsset)(void* font);
	void (*TMP_Text_set_font)(void* _this, void* fontAsset);
	void* (*TMP_Text_get_font)(void* _this);
	// void* lastUpdateFontPtr = nullptr;
	std::unordered_set<void*> updatedFontPtrs{};

	void* UITextMeshProUGUI_Awake_orig;
	void UITextMeshProUGUI_Awake_hook(void* _this) {
		static auto get_Text = reinterpret_cast<Il2CppString * (*)(void*)>(
			il2cpp_symbols::get_method_pointer(
				"ENTERPRISE.UI.dll", "ENTERPRISE.UI",
				"UITextMeshProUGUI", "get_text", 0
			)
			);
		static auto set_Text = reinterpret_cast<void (*)(void*, Il2CppString*)>(
			il2cpp_symbols::get_method_pointer(
				"ENTERPRISE.UI.dll", "ENTERPRISE.UI",
				"UITextMeshProUGUI", "set_text", 1
			)
			);

		static auto get_LocalizeOnAwake = reinterpret_cast<bool (*)(void*)>(
			il2cpp_symbols::get_method_pointer(
				"ENTERPRISE.UI.dll", "ENTERPRISE.UI",
				"UITextMeshProUGUI", "get_LocalizeOnAwake", 0
			)
			);

		auto origText = get_Text(_this);
		if (origText) {
			// wprintf(L"UITextMeshProUGUI_Awake: %ls\n", origText->start_char);
			if (!get_LocalizeOnAwake(_this)) {
				std::string newTrans("");
				if (SCLocal::getGameUnlocalTrans(std::wstring(origText->start_char), &newTrans)) {
					set_Text(_this, il2cpp_string_new(newTrans.c_str()));
				}
			}
			else {
				// set_Text(_this, il2cpp_symbols::NewWStr(std::format(L"(接口l){}", std::wstring(origText->start_char))));
			}
		}

		static auto set_sourceFontFile = reinterpret_cast<void (*)(void*, void* font)>(
			il2cpp_symbols::get_method_pointer("Unity.TextMeshPro.dll", "TMPro",
				"TMP_FontAsset", "set_sourceFontFile", 1)
			);
		static auto UpdateFontAssetData = reinterpret_cast<void (*)(void*)>(
			il2cpp_symbols::get_method_pointer("Unity.TextMeshPro.dll", "TMPro",
				"TMP_FontAsset", "UpdateFontAssetData", 0)
			);
		static auto set_fontSize = reinterpret_cast<void (*)(void*, float)>(
			il2cpp_symbols::get_method_pointer("Unity.TextMeshPro.dll", "TMPro",
				"TMP_Text", "set_fontSize", 1)
			);
		static auto get_fontSize = reinterpret_cast<float (*)(void*)>(
			il2cpp_symbols::get_method_pointer("Unity.TextMeshPro.dll", "TMPro",
				"TMP_Text", "get_fontSize", 0)
			);

		auto replaceFont = getReplaceFont();
		if (replaceFont) {
			auto origFont = TMP_Text_get_font(_this);
			set_sourceFontFile(origFont, replaceFont);
			if (!updatedFontPtrs.contains(origFont)) {
				updatedFontPtrs.emplace(origFont);
				UpdateFontAssetData(origFont);
			}
			/*
			if (origFont != lastUpdateFontPtr) {
				UpdateFontAssetData(origFont);
				lastUpdateFontPtr = origFont;
			}*/
		}
		set_fontSize(_this, get_fontSize(_this) + g_font_size_offset);
		reinterpret_cast<decltype(UITextMeshProUGUI_Awake_hook)*>(UITextMeshProUGUI_Awake_orig)(_this);
	}

	void* ScenarioManager_Init_orig;
	void* ScenarioManager_Init_hook(void* retstr, void* _this, Il2CppString* scrName) {
		// printf("ScenarioManager_Init: %ls\n%ls\n\n", scrName->start_char, environment_get_stacktrace()->start_char);
		return reinterpret_cast<decltype(ScenarioManager_Init_hook)*>(ScenarioManager_Init_orig)(retstr, _this, scrName);
	}

	void* (*ReadAllBytes)(Il2CppString*);
	void* readFileAllBytes(const std::wstring& name) {
		return ReadAllBytes(il2cpp_symbols::NewWStr(name));
	}

	void* DataFile_GetBytes_hook(Il2CppString* path) {
		std::wstring pathStr(path->start_char);
		std::wstring_view pathStrView(pathStr);
		//wprintf(L"DataFile_GetBytes: %ls\n", pathStr.c_str());

		
		std::filesystem::path localFileName;
		if (SCLocal::getLocalFileName(pathStr, &localFileName)) {
			return readFileAllBytes(localFileName);
		}

		auto ret = reinterpret_cast<decltype(DataFile_GetBytes_hook)*>(DataFile_GetBytes_orig)(path);
		if (g_auto_dump_all_json) {
			if (pathStrView.ends_with(L".json")) {
				auto basePath = std::filesystem::path("dumps") / "autoDumpJson";
				std::filesystem::path fileName = basePath / SCLocal::getFilePathByName(pathStr, true, basePath);
				
				if (!std::filesystem::is_directory(basePath)) {
					std::filesystem::create_directories(basePath);
				}
				auto newStr = bytesToIl2cppString(ret);
				std::ofstream fileDump(fileName, std::ofstream::out);
				auto writeWstr = std::wstring(newStr->start_char);

				const std::wstring searchStr = L"\r";
				const std::wstring replaceStr = L"\\r";
				size_t pos = writeWstr.find(searchStr);
				while (pos != std::wstring::npos) {
					writeWstr.replace(pos, 1, replaceStr);
					pos = writeWstr.find(searchStr, pos + replaceStr.length());
				}

				fileDump << utility::conversions::to_utf8string(writeWstr).c_str();
				fileDump.close();
				printf("Auto dump: %ls\n", fileName.c_str());
			}
		}
		return ret;
	}

	void SetResolution_hook(int width, int height, bool fullscreen) {
		return;
		/*
		width = SCGUIData::screenW;
		height = SCGUIData::screenH;
		fullscreen = SCGUIData::screenFull;
		return reinterpret_cast<decltype(SetResolution_hook)*>(SetResolution_orig)(width, height, fullscreen);
		*/
	}

	void* InvokeMoveNext_orig;
	void InvokeMoveNext_hook(void* enumerator, void* returnValueAddress) {
		MHotkey::start_hotkey(hotKey);
		return reinterpret_cast<decltype(InvokeMoveNext_hook)*>(InvokeMoveNext_orig)(enumerator, returnValueAddress);
	}

	void* Live_SetEnableDepthOfField_orig;
	void Live_SetEnableDepthOfField_hook(void* _this, bool isEnable) {
		if (g_enable_free_camera) {
			isEnable = false;
		}
		return reinterpret_cast<decltype(Live_SetEnableDepthOfField_hook)*>(Live_SetEnableDepthOfField_orig)(_this, isEnable);
	}

	void* Live_Update_orig;
	void Live_Update_hook(void* _this) {
		reinterpret_cast<decltype(Live_Update_hook)*>(Live_Update_orig)(_this);
		if (g_enable_free_camera) {
			Live_SetEnableDepthOfField_hook(_this, false);
		}
	}

	bool isCancelTryOn = false;
	void* LiveCostumeChangeView_setTryOnMode_orig;
	void LiveCostumeChangeView_setTryOnMode_hook(void* _this, void* idol, bool isTryOn) {
		if (!isTryOn) isCancelTryOn = true;
		reinterpret_cast<decltype(LiveCostumeChangeView_setTryOnMode_hook)*>(LiveCostumeChangeView_setTryOnMode_orig)(_this, idol, isTryOn);
		isCancelTryOn = false;
	}

	void* LiveCostumeChangeView_setIdolCostume_orig;
	void LiveCostumeChangeView_setIdolCostume_hook(void* _this, void* idol, int category, int costumeId) {
		if (g_allow_use_tryon_costume) {
			static auto iidol_klass = il2cpp_symbols::get_class_from_instance(idol);
			static auto get_CharacterId_mtd = il2cpp_class_get_method_from_name(iidol_klass, "get_CharacterId", 0);
			if (get_CharacterId_mtd) {
				const auto idolId = reinterpret_cast<int (*)(void*)>(get_CharacterId_mtd->methodPointer)(idol);
				printf("LiveCostumeChangeView_setIdolCostume idol: %d, category: %d, costumeId: %d\n", idolId, category, costumeId);
			}
			if (isCancelTryOn) return;
		}

		return reinterpret_cast<decltype(LiveCostumeChangeView_setIdolCostume_hook)*>(LiveCostumeChangeView_setIdolCostume_orig)(_this, idol, category, costumeId);
	}

	void* (*LiveCostumeChangeModel_get_Idol)(void*);

#define replaceResourceId(currHook, currOrig) \
	const auto idol = LiveCostumeChangeModel_get_Idol(_this);\
	static auto iidol_klass = il2cpp_symbols::get_class_from_instance(idol);\
	static auto get_CharacterId_mtd = il2cpp_class_get_method_from_name(iidol_klass, "get_CharacterId", 0);\
	const auto idolId = reinterpret_cast<int (*)(void*)>(get_CharacterId_mtd->methodPointer)(idol);\
	const auto resId = il2cpp_symbols::read_field<int>(ret, resourceId_field);\
	printf(#currHook" idol: %d, dressId: %d, ResId: %d, this at %p\n", idolId, id, resId, _this);\
	\
	int replaceDressId = baseId + idolId * 1000;\
	\
	auto replaceResId = id == replaceDressId ? baseResId : il2cpp_symbols::read_field<int>(ret, resourceId_field);\
	auto newRet = reinterpret_cast<decltype(currHook)*>(currOrig)(_this, replaceDressId);\
	if (!newRet) {\
		printf("ReplaceId: %d not found. try++\n", replaceDressId);\
		replaceDressId++;\
		newRet = reinterpret_cast<decltype(currHook)*>(currOrig)(_this, replaceDressId);\
		if (!newRet){\
			printf("replaceId: %d not found. Replace failed.\n", replaceDressId);\
			return ret;\
		}\
		replaceResId = id == replaceDressId ? baseResId : il2cpp_symbols::read_field<int>(ret, resourceId_field);\
	}\
	il2cpp_symbols::write_field(newRet, resourceId_field, replaceResId)


	void* LiveCostumeChangeModel_GetDress_orig;
	void* LiveCostumeChangeModel_GetDress_hook(void* _this, int id) {  // 替换服装 ResID
		auto ret = reinterpret_cast<decltype(LiveCostumeChangeModel_GetDress_hook)*>(LiveCostumeChangeModel_GetDress_orig)(_this, id);
		if (!g_unlock_all_dress) return ret;
		if (!ret) {
			printf("LiveCostumeChangeModel_GetDress returns NULL ResId: %d, this at %p\n", id, _this); 
			return ret; 
		}
		static auto ret_klass = il2cpp_symbols::get_class_from_instance(ret); 
		static auto resourceId_field = il2cpp_class_get_field_from_name(ret_klass, "resourceId_"); 

		const auto baseId = 100001;
		const auto baseResId = 1;
		replaceResourceId(LiveCostumeChangeModel_GetDress_hook, LiveCostumeChangeModel_GetDress_orig);
		return newRet;
	}

	void* LiveCostumeChangeModel_GetAccessory_orig;
	void* LiveCostumeChangeModel_GetAccessory_hook(void* _this, int id) {  // 替换饰品 ResID
		auto ret = reinterpret_cast<decltype(LiveCostumeChangeModel_GetAccessory_hook)*>(LiveCostumeChangeModel_GetAccessory_orig)(_this, id);
		if (!(g_unlock_all_dress && g_unlock_all_headwear)) return ret;
		if (!ret) {
			printf("LiveCostumeChangeModel_GetAccessory returns NULL ResId: %d, this at %p\n", id, _this);
			return ret;
		}
		static auto ret_klass = il2cpp_symbols::get_class_from_instance(ret);
		static auto resourceId_field = il2cpp_class_get_field_from_name(ret_klass, "resourceId_");
		static auto accessoryType_field = il2cpp_class_get_field_from_name(ret_klass, "accessoryType_");

		const auto accessoryType = il2cpp_symbols::read_field<int>(ret, accessoryType_field);
		printf("accessoryType: %d\n", accessoryType);
		int baseId;
		int baseResId;
		switch (accessoryType) {
		case 0x1: {  // Glasses
			baseId = 800006;
			baseResId = 1103400;
		}; break;
		case 0x2: {  // Earrings
			baseId = 800001;
			baseResId = 2101300;
		}; break;
		case 0x3: {  // Makeup
			baseId = 800009;
			baseResId = 9100103;
		}; break;
		default: return ret;
		}

		replaceResourceId(LiveCostumeChangeModel_GetAccessory_hook, LiveCostumeChangeModel_GetAccessory_orig);
		// LiveCostumeChangeModel_GetAccessory_hook idol : 15, dressId : 815004, ResId : 1103200, this at 0000021E62ADDD80
		return newRet;
	}

	void* LiveCostumeChangeModel_GetHairstyle_orig;
	void* LiveCostumeChangeModel_GetHairstyle_hook(void* _this, int id) {  // 替换头发 ResID
		auto ret = reinterpret_cast<decltype(LiveCostumeChangeModel_GetHairstyle_hook)*>(LiveCostumeChangeModel_GetHairstyle_orig)(_this, id);
		if (!(g_unlock_all_dress && g_unlock_all_headwear)) return ret;
		if (!ret) {
			printf("LiveCostumeChangeModel_GetHairstyle returns NULL ResId: %d, this at %p\n", id, _this);
			return ret;
		}
		static auto ret_klass = il2cpp_symbols::get_class_from_instance(ret);
		static auto resourceId_field = il2cpp_class_get_field_from_name(ret_klass, "resourceId_");

		const auto baseId = 600001;
		const auto baseResId = 1;
		replaceResourceId(LiveCostumeChangeModel_GetHairstyle_hook, LiveCostumeChangeModel_GetHairstyle_orig);
		
		static auto accessoryResourceIdList_field = il2cpp_class_get_field_from_name(ret_klass, "accessoryResourceIdList_");
		const auto replaceaccessoryResourceIdList = il2cpp_symbols::read_field(ret, accessoryResourceIdList_field);
		if (id == replaceDressId) {
			static auto RepeatedField_klass = il2cpp_symbols::get_class_from_instance(replaceaccessoryResourceIdList);
			static auto RepeatedField_Add_mtd = il2cpp_class_get_method_from_name(RepeatedField_klass, "Add", 1);
			static auto RepeatedField_Add = reinterpret_cast<void (*)(void*, int, void* mtd)>(RepeatedField_Add_mtd->methodPointer);
			static auto RepeatedField_ctor_mtd = il2cpp_class_get_method_from_name(RepeatedField_klass, ".ctor", 0);
			static auto RepeatedField_ctor = reinterpret_cast<void (*)(void*, void* mtd)>(RepeatedField_ctor_mtd->methodPointer);

			auto newRepeatedField = il2cpp_object_new(RepeatedField_klass);
			RepeatedField_ctor(newRepeatedField, RepeatedField_ctor_mtd);
			RepeatedField_Add(newRepeatedField, 0, RepeatedField_ctor_mtd);
			RepeatedField_Add(newRepeatedField, 0, RepeatedField_ctor_mtd);
			il2cpp_symbols::write_field(newRet, accessoryResourceIdList_field, newRepeatedField);
		}
		else {
			il2cpp_symbols::write_field(newRet, accessoryResourceIdList_field, replaceaccessoryResourceIdList);
		}
		
		return newRet;
	}

	bool confirmationingModel = false;
	std::map<int, void*> cacheDressMap{};
	std::map<int, void*> cacheHairMap{};
	std::map<int, void*> cacheAccessoryMap{};

	void* LiveCostumeChangeModel_ctor_orig;
	void LiveCostumeChangeModel_ctor_hook(void* _this, void* reply, void* idol, int costumeType) {  // 添加服装到 dressDic
		/*
		static auto iidol_klass = il2cpp_symbols::get_class_from_instance(idol);
		static auto get_CharacterId_mtd = il2cpp_class_get_method_from_name(iidol_klass, "get_CharacterId", 0);
		if (get_CharacterId_mtd) {
			const auto idolId = reinterpret_cast<int (*)(void*)>(get_CharacterId_mtd->methodPointer)(idol);
			printf("LiveCostumeChangeModel_ctor idolId: %d, costumeType: %d\n", idolId, costumeType);
		}
		*/
		reinterpret_cast<decltype(LiveCostumeChangeModel_ctor_hook)*>(LiveCostumeChangeModel_ctor_orig)(_this, reply, idol, costumeType);

		if (g_unlock_all_dress && (costumeType == 1)) {
			static auto LiveCostumeChangeModel_klass = il2cpp_symbols::get_class("PRISM.Adapters.dll", "PRISM.Adapters", "LiveCostumeChangeModel");
			static auto dressDic_field = il2cpp_class_get_field_from_name(LiveCostumeChangeModel_klass, "dressDic");
			static auto hairstyleDic_field = il2cpp_class_get_field_from_name(LiveCostumeChangeModel_klass, "hairstyleDic");
			static auto accessoryDic_field = il2cpp_class_get_field_from_name(LiveCostumeChangeModel_klass, "accessoryDic");

			auto dressDicInstance = il2cpp_field_get_value_object(dressDic_field, _this);
			// auto hairstyleDicInstance = il2cpp_field_get_value_object(hairstyleDic_field, _this);
			// auto accessoryDicInstance = il2cpp_field_get_value_object(accessoryDic_field, _this);

			auto dressDic = Utils::CSDictEditor<int>(dressDicInstance, "System.Collections.Generic.Dictionary`2[System.Int32, PRISM.Module.Networking.ICostumeStatus]");
			// auto hairstyleDic = Utils::CSDictEditor<int>(hairstyleDicInstance, "System.Collections.Generic.Dictionary`2[System.Int32, PRISM.Module.Networking.IHairstyleStatus]");
			// auto accessoryDic = Utils::CSDictEditor<int>(accessoryDicInstance, "System.Collections.Generic.Dictionary`2[System.Int32, PRISM.Module.Networking.IAccessoryStatus]");

			const auto addToDic = [](std::map<int, void*>& cacheMap, Utils::CSDictEditor<int>& dict) {
				for (auto& i : cacheMap) {
					const auto currId = i.first;
					if (!dict.ContainsKey(currId)) {
						auto& costumeStat = i.second;
						// printf("add: %p\n", costumeStat);
						dict.Add(currId, costumeStat);
					}
				}};

			addToDic(cacheDressMap, dressDic);
			// addToDic(cacheHairMap, hairstyleDic);  // TODO PRISM.ResourceManagement.ResourceLoader._throwMissingKeyException
			// addToDic(cacheAccessoryMap, accessoryDic);  // TODO PRISM.ResourceManagement.ResourceLoader._throwMissingKeyException
		}
		
	}

	void AssembleCharacter_ApplyParam_hook(void* mdl, float height, float bust, float head, float arm, float hand) {
		if (g_enable_chara_param_edit) {
			static auto get_ObjectName = reinterpret_cast<Il2CppString * (*)(void*)>(il2cpp_resolve_icall("UnityEngine.Object::GetName(UnityEngine.Object)"));
			const auto objNameIlStr = get_ObjectName(mdl);
			const std::string objName = objNameIlStr ? utility::conversions::to_utf8string(std::wstring(objNameIlStr->start_char)) : std::format("Unnamed Obj {:p}", mdl);
			std::string showObjName;
			if (objName.starts_with("m_ALL_")) {
				const auto nextFlagPos = objName.find_first_of(L'_', 6);
				if (nextFlagPos > 6) {
					showObjName = objName.substr(0, nextFlagPos);
				}
				else {
					showObjName = objName;
				}
			}
			else {
				showObjName = objName;
			}
			if (auto it = charaParam.find(showObjName); it != charaParam.end()) {
				it->second.SetObjPtr(mdl);
				it->second.UpdateParam(&height, &bust, &head, &arm, &hand);
				return it->second.Apply();
			}
			else {
				charaParam.emplace(showObjName, CharaParam_t(height, bust, head, arm, hand, mdl));
			}
		}
		return reinterpret_cast<decltype(AssembleCharacter_ApplyParam_hook)*>(AssembleCharacter_ApplyParam_orig)(mdl, height, bust, head, arm, hand);
	}

	void* MainThreadDispatcher_LateUpdate_orig;
	void MainThreadDispatcher_LateUpdate_hook(void* _this, void* method) {
		try {
			auto it = mainThreadTasks.begin();
			while (it != mainThreadTasks.end()) {
				try {
					bool result = (*it)();
					if (result) {
						it = mainThreadTasks.erase(it);
					}
					else {
						++it;
					}
				}
				catch (std::exception& e) {
					printf("MainThreadDispatcher Tasks Error: %s\n", e.what());
					it = mainThreadTasks.erase(it);
				}
			}
		}
		catch (std::exception& ex) {
			printf("MainThreadDispatcher Error: %s\n", ex.what());
		}
		return reinterpret_cast<decltype(MainThreadDispatcher_LateUpdate_hook)*>(MainThreadDispatcher_LateUpdate_orig)(_this, method);
	}

	void checkAndAddCostume(void* _this, int key, void* value, MethodInfo* method) {
		static auto CostumeStatus_klass = il2cpp_symbols::get_class("PRISM.Module.Networking.dll",
			"PRISM.Module.Networking.Stub.Status", "CostumeStatus");
		static auto HairstyleStatus_klass = il2cpp_symbols::get_class("PRISM.Module.Networking.dll",
			"PRISM.Module.Networking.Stub.Status", "HairstyleStatus");
		static auto AccessoryStatus_klass = il2cpp_symbols::get_class("PRISM.Module.Networking.dll",
			"PRISM.Module.Networking.Stub.Status", "AccessoryStatus");
		auto value_klass = il2cpp_symbols::get_class_from_instance(value);

#define unlockAndAddToMap(klass, cacheMap) \
		static auto klass##_isUnlocked_field = il2cpp_class_get_field_from_name(klass, "isUnlocked_"); \
		il2cpp_symbols::write_field(value, klass##_isUnlocked_field, true); \
		cacheMap.emplace(key, value)

		if (value_klass == CostumeStatus_klass) {
			// wprintf(L"dic_int_ICostumeStatus_add: this: %p, key: %d\n", _this, key);
			unlockAndAddToMap(CostumeStatus_klass, cacheDressMap);
		}
		else if (value_klass == HairstyleStatus_klass) {
			if (g_unlock_all_headwear) {
				unlockAndAddToMap(HairstyleStatus_klass, cacheHairMap);
			}
		}
		else if (value_klass == AccessoryStatus_klass) {
			if (g_unlock_all_headwear) {
				unlockAndAddToMap(AccessoryStatus_klass, cacheAccessoryMap);
			}
		}
	}

	void* dic_int_ICostumeStatus_add_orig;
	void dic_int_ICostumeStatus_add_hook(void* _this, int key, void* value, MethodInfo* method) {  // 添加服装到缓存表
		if ((g_unlock_all_dress || g_allow_use_tryon_costume) && confirmationingModel) checkAndAddCostume(_this, key, value, method);
		return reinterpret_cast<decltype(dic_int_ICostumeStatus_add_hook)*>(dic_int_ICostumeStatus_add_orig)(_this, key, value, method);
	}

	void* LiveMVUnitConfirmationModel_ctor_orig;
	void LiveMVUnitConfirmationModel_ctor_hook(void* _this, void* musicData, void* mvUnitListReply, void* costumeListReply) {
		confirmationingModel = true;
		cacheDressMap.clear();
		cacheHairMap.clear();
		cacheAccessoryMap.clear();

		reinterpret_cast<decltype(LiveMVUnitConfirmationModel_ctor_hook)*>(LiveMVUnitConfirmationModel_ctor_orig)(_this, musicData, mvUnitListReply, costumeListReply);
		confirmationingModel = false;
		return;
	}

	void* PopupSystem_ShowPopup_orig;
	// 调试用，必要时 HOOK
	void* PopupSystem_ShowPopup_hook(void* _this, void* iObject, int iPriority, bool isFullSize) {
		wprintf(L"PopupSystem_ShowPopup_hook:\n%ls\n\n", environment_get_stacktrace()->start_char);
		return reinterpret_cast<decltype(PopupSystem_ShowPopup_hook)*>(PopupSystem_ShowPopup_orig)(_this, iObject, iPriority, isFullSize);
	}

	void* LiveMVUnit_GetMemberChangeRequestData_orig;
	void* LiveMVUnit_GetMemberChangeRequestData_hook(void* _this, int position, void* idol, int exchangePosition) {
		if (g_allow_same_idol) {
			exchangePosition = -1;
		}
		return reinterpret_cast<decltype(LiveMVUnit_GetMemberChangeRequestData_hook)*>(LiveMVUnit_GetMemberChangeRequestData_orig)(_this, position, idol, exchangePosition);
	}

	void* CriWareErrorHandler_HandleMessage_orig;
	void CriWareErrorHandler_HandleMessage_hook(void* _this, Il2CppString* msg) {
		// wprintf(L"CriWareErrorHandler_HandleMessage: %ls\n%ls\n\n", msg->start_char, environment_get_stacktrace()->start_char);
		environment_get_stacktrace();
		return reinterpret_cast<decltype(CriWareErrorHandler_HandleMessage_hook)*>(CriWareErrorHandler_HandleMessage_orig)(_this, msg);
	}

	void* UnsafeLoadBytesFromKey_orig;
	void* UnsafeLoadBytesFromKey_hook(void* _this, Il2CppString* tagName, Il2CppString* assetKey) {
		auto ret = reinterpret_cast<decltype(UnsafeLoadBytesFromKey_hook)*>(UnsafeLoadBytesFromKey_orig)(_this, tagName, assetKey);
		// wprintf(L"UnsafeLoadBytesFromKey: tag: %ls, assetKey: %ls\n", tagName->start_char, assetKey->start_char);
		// dumpByteArray(tagName->start_char, assetKey->start_char, ret);
		return ret;
	}

	void* TextLog_AddLog_orig;
	void TextLog_AddLog_hook(void* _this, int speakerFlag, Il2CppString* textID, Il2CppString* text, bool isChoice) {
		return reinterpret_cast<decltype(TextLog_AddLog_hook)*>(TextLog_AddLog_orig)(_this, speakerFlag, textID, text, isChoice);

		static auto TextLog_klass = il2cpp_symbols::get_class_from_instance(_this);
		static auto dicConvertID_field = il2cpp_class_get_field_from_name(TextLog_klass, "dicConvertID");
		static auto speakerTable_field = il2cpp_class_get_field_from_name(TextLog_klass, "speakerTable");
		static auto listTextLogData_field = il2cpp_class_get_field_from_name(TextLog_klass, "listTextLogData");
		static auto unitIdol_field = il2cpp_class_get_field_from_name(TextLog_klass, "unitIdol");
		static auto convertList_field = il2cpp_class_get_field_from_name(TextLog_klass, "convertList");

		static auto toJsonStr = reinterpret_cast<Il2CppString * (*)(void*)>(
			il2cpp_symbols::get_method_pointer("Newtonsoft.Json.dll", "Newtonsoft.Json",
				"JsonConvert", "SerializeObject", 1)
			);

		auto dicConvertID = il2cpp_field_get_value_object(dicConvertID_field, _this);
		auto speakerTable = il2cpp_field_get_value_object(speakerTable_field, _this);
		auto listTextLogData = il2cpp_field_get_value_object(listTextLogData_field, _this);
		auto unitIdol = il2cpp_field_get_value_object(unitIdol_field, _this);
		auto convertList = il2cpp_field_get_value_object(convertList_field, _this);

		wprintf(L"TextLog_AddLog: speakerFlag: %d, textID: %ls, text: %ls\ndicConvertID:\n%ls\n\nspeakerTable:\n%ls\n\nlistTextLogData:\n%ls\n\nunitIdol:\n%ls\n\nconvertList:\n%ls\n\n", 
			speakerFlag, textID->start_char, text->start_char,
			toJsonStr(dicConvertID)->start_char,
			toJsonStr(speakerTable)->start_char,
			toJsonStr(listTextLogData)->start_char,
			toJsonStr(unitIdol)->start_char,
			toJsonStr(convertList)->start_char
		);
	}

	void* baseCameraTransform = nullptr;
	void* baseCamera = nullptr;

	void* Unity_set_pos_injected_orig;
	void Unity_set_pos_injected_hook(void* _this, Vector3_t* ret) {
		return reinterpret_cast<decltype(Unity_set_pos_injected_hook)*>(Unity_set_pos_injected_orig)(_this, ret);
	}

	void* Unity_get_pos_injected_orig;
	void Unity_get_pos_injected_hook(void* _this, Vector3_t* data) {
		reinterpret_cast<decltype(Unity_get_pos_injected_hook)*>(Unity_get_pos_injected_orig)(_this, data);

		if (_this == baseCameraTransform) {
			if (guiStarting) {
				SCGUIData::sysCamPos.x = data->x;
				SCGUIData::sysCamPos.y = data->y;
				SCGUIData::sysCamPos.z = data->z;
			}
			if (g_enable_free_camera) {
				SCCamera::baseCamera.updateOtherPos(data);
				Unity_set_pos_injected_hook(_this, data);
			}
		}
	}

	void* Unity_set_fieldOfView_orig;
	void Unity_set_fieldOfView_hook(void* _this, float single) {
		return reinterpret_cast<decltype(Unity_set_fieldOfView_hook)*>(Unity_set_fieldOfView_orig)(_this, single);
	}
	void* Unity_get_fieldOfView_orig;
	float Unity_get_fieldOfView_hook(void* _this) {
		const auto origFov = reinterpret_cast<decltype(Unity_get_fieldOfView_hook)*>(Unity_get_fieldOfView_orig)(_this);
		if (_this == baseCamera) {
			if (guiStarting) {
				SCGUIData::sysCamFov = origFov;
			}
			if (g_enable_free_camera) {
				const auto fov = SCCamera::baseCamera.fov;
				Unity_set_fieldOfView_hook(_this, fov);
				return fov;
			}
		}
		// printf("get_fov: %f\n", ret);
		return origFov;
	}

	void* Unity_LookAt_Injected_orig;
	void Unity_LookAt_Injected_hook(void* _this, Vector3_t* worldPosition, Vector3_t* worldUp) {
		if (_this == baseCameraTransform) {
			if (g_enable_free_camera) {
				auto pos = SCCamera::baseCamera.getLookAt();
				worldPosition->x = pos.x;
				worldPosition->y = pos.y;
				worldPosition->z = pos.z;
			}
		}
		return reinterpret_cast<decltype(Unity_LookAt_Injected_hook)*>(Unity_LookAt_Injected_orig)(_this, worldPosition, worldUp);
	}
	void* Unity_set_nearClipPlane_orig;
	void Unity_set_nearClipPlane_hook(void* _this, float single) {
		if (_this == baseCamera) {
			if (g_enable_free_camera) {
				single = 0.001f;
			}
		}
		return reinterpret_cast<decltype(Unity_set_nearClipPlane_hook)*>(Unity_set_nearClipPlane_orig)(_this, single);
	}

	void* Unity_get_nearClipPlane_orig;
	float Unity_get_nearClipPlane_hook(void* _this) {
		auto ret = reinterpret_cast<decltype(Unity_get_nearClipPlane_hook)*>(Unity_get_nearClipPlane_orig)(_this);
		if (_this == baseCamera) {
			if (g_enable_free_camera) {
				ret = 0.001f;
			}
		}
		return ret;
	}
	void* Unity_get_farClipPlane_orig;
	float Unity_get_farClipPlane_hook(void* _this) {
		auto ret = reinterpret_cast<decltype(Unity_get_farClipPlane_hook)*>(Unity_get_farClipPlane_orig)(_this);
		if (_this == baseCamera) {
			if (g_enable_free_camera) {
				ret = 2500.0f;
			}
		}
		return ret;
	}

	void* Unity_set_farClipPlane_orig;
	void Unity_set_farClipPlane_hook(void* _this, float value) {
		if(_this == baseCamera) {
			if (g_enable_free_camera) {
				value = 2500.0f;
			}
		}
		reinterpret_cast<decltype(Unity_set_farClipPlane_hook)*>(Unity_set_farClipPlane_orig)(_this, value);
	}

	void* Unity_set_rotation_Injected_orig;
	void Unity_set_rotation_Injected_hook(void* _this, Quaternion_t* value) {
		return reinterpret_cast<decltype(Unity_set_rotation_Injected_hook)*>(Unity_set_rotation_Injected_orig)(_this, value);
	}
	void* Unity_get_rotation_Injected_orig;
	void Unity_get_rotation_Injected_hook(void* _this, Quaternion_t* ret) {
		reinterpret_cast<decltype(Unity_get_rotation_Injected_hook)*>(Unity_get_rotation_Injected_orig)(_this, ret);

		if (_this == baseCameraTransform) {
			if (guiStarting) {
				SCGUIData::sysCamRot.w = ret->w;
				SCGUIData::sysCamRot.x = ret->x;
				SCGUIData::sysCamRot.y = ret->y;
				SCGUIData::sysCamRot.z = ret->z;
				SCGUIData::updateSysCamLookAt();
			}
			if (g_enable_free_camera) {
				ret->w = 0;
				ret->x = 0;
				ret->y = 0;
				ret->z = 0;
				Unity_set_rotation_Injected_hook(_this, ret);

				static auto Vector3_klass = il2cpp_symbols::get_class("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
				Vector3_t* pos = reinterpret_cast<Vector3_t*>(il2cpp_object_new(Vector3_klass));
				Vector3_t* up = reinterpret_cast<Vector3_t*>(il2cpp_object_new(Vector3_klass));
				up->x = 0;
				up->y = 1;
				up->z = 0;
				Unity_LookAt_Injected_hook(_this, pos, up);
			}
		}
	}


	void* get_baseCamera_orig;
	void* get_baseCamera_hook(void* _this) {
		// printf("get_baseCamera\n");
		auto ret = reinterpret_cast<decltype(get_baseCamera_hook)*>(get_baseCamera_orig)(_this);  // UnityEngine.Camera

		if (g_enable_free_camera || guiStarting) {
			static auto GetComponent = reinterpret_cast<void* (*)(void*, Il2CppReflectionType*)>(
				il2cpp_symbols::get_method_pointer("UnityEngine.CoreModule.dll", "UnityEngine",
					"Component", "GetComponent", 1));

			static auto transClass = il2cpp_symbols::get_class("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
			static auto transType = il2cpp_type_get_object(il2cpp_class_get_type(transClass));

			auto transform = GetComponent(ret, transType);  // UnityEngine.Transform
			if (transform) {
				baseCameraTransform = transform;
				baseCamera = ret;
			}
		}

		return ret;
	}

	void readDMMGameGuardData();

	void* GGIregualDetector_ShowPopup_orig;
	void GGIregualDetector_ShowPopup_hook(void* _this, int code) {
		//printf("GGIregualDetector_ShowPopup: code: %d\n%ls\n\n", code, environment_get_stacktrace()->start_char);
		//readDMMGameGuardData();

		static auto this_klass = il2cpp_symbols::get_class_from_instance(_this);
		static FieldInfo* isShowPopup_field = il2cpp_class_get_field_from_name(this_klass, "_isShowPopup");
		static FieldInfo* isError_field = il2cpp_class_get_field_from_name(this_klass, "_isError");
		auto fieldData = il2cpp_class_get_static_field_data(this_klass);
		auto isError = il2cpp_symbols::read_field<bool>(fieldData, isError_field);

		if (isError) {
			// if (code == 0) return;
		}

		return reinterpret_cast<decltype(GGIregualDetector_ShowPopup_hook)*>(GGIregualDetector_ShowPopup_orig)(_this, code);
	}

	void* DMMGameGuard_NPGameMonCallback_orig;
	int DMMGameGuard_NPGameMonCallback_hook(UINT dwMsg, UINT dwArg) {
		// printf("DMMGameGuard_NPGameMonCallback: dwMsg: %u, dwArg: %u\n%ls\n\n", dwMsg, dwArg, environment_get_stacktrace()->start_char);
		return 1;
		// return reinterpret_cast<decltype(DMMGameGuard_NPGameMonCallback_hook)*>(DMMGameGuard_NPGameMonCallback_orig)(dwMsg, dwArg);
	}

	void* set_vsync_count_orig;
	void set_vsync_count_hook(int value) {
		return reinterpret_cast<decltype(set_vsync_count_hook)*>(set_vsync_count_orig)(g_vsync_count == -1 ? value : g_vsync_count);
	}

	void* set_fps_orig;
	void set_fps_hook(int value) {
		return reinterpret_cast<decltype(set_fps_hook)*>(set_fps_orig)(g_max_fps == -1 ? value : g_max_fps);
	}

	void* Unity_Quit_orig;
	void Unity_Quit_hook(int code) {
		printf("Quit code: %d\n", code);
		// printf("Quit code: %d\n%ls\n\n", code, environment_get_stacktrace()->start_char);
	}

	void* SetCallbackToGameMon_orig;
	int SetCallbackToGameMon_hook(void* DMMGameGuard_NPGameMonCallback_addr) {
		// printf("SetCallbackToGameMon: %p\n%ls\n\n", DMMGameGuard_NPGameMonCallback_addr, environment_get_stacktrace()->start_char);
		ADD_HOOK(DMMGameGuard_NPGameMonCallback, "DMMGameGuard_NPGameMonCallback at %p");
		return reinterpret_cast<decltype(SetCallbackToGameMon_hook)*>(SetCallbackToGameMon_orig)(DMMGameGuard_NPGameMonCallback_addr);
	}

	void* DMMGameGuard_Setup_orig;
	void DMMGameGuard_Setup_hook() {
		// printf("\n\nDMMGameGuard_Setup\n\n");
		readDMMGameGuardData();
	}

	void* DMMGameGuard_SetCheckMode_orig;
	void DMMGameGuard_SetCheckMode_hook(bool isCheck) {
		// printf("DMMGameGuard_SetCheckMode before\n");
		// readDMMGameGuardData();
		reinterpret_cast<decltype(DMMGameGuard_SetCheckMode_hook)*>(DMMGameGuard_SetCheckMode_orig)(isCheck);
		// printf("DMMGameGuard_SetCheckMode after\n");
		// readDMMGameGuardData();
	}

	void* PreInitNPGameMonW_orig;
	void PreInitNPGameMonW_hook(int64_t v) {
		// printf("PreInitNPGameMonW\n");
		// readDMMGameGuardData();
	}
	void* PreInitNPGameMonA_orig;
	void PreInitNPGameMonA_hook(int64_t v) {
		// printf("PreInitNPGameMonA\n");
		// readDMMGameGuardData();
	}

	void* InitNPGameMon_orig;
	UINT InitNPGameMon_hook() {
		// printf("InitNPGameMon\n");
		// readDMMGameGuardData();
		return 1877;
	}

	void* SetHwndToGameMon_orig;
	void SetHwndToGameMon_hook(int64_t v) {
		// printf("SetHwndToGameMon\n");
		// readDMMGameGuardData();
	}

	void* DMMGameGuard_klass;
	FieldInfo* isCheck_field;
	FieldInfo* isInit_field;
	FieldInfo* bAppExit_field;
	FieldInfo* errCode_field;

	void readDMMGameGuardData() {
		if (!DMMGameGuard_klass) return;
		if (!isCheck_field) {
			printf("isCheck_field invalid: %p\n", isCheck_field);
			return;
		}

		auto staticData = il2cpp_class_get_static_field_data(DMMGameGuard_klass);
		auto isCheck = il2cpp_symbols::read_field<bool>(staticData, isCheck_field);
		auto isInit = il2cpp_symbols::read_field<bool>(staticData, isInit_field);
		auto bAppExit = il2cpp_symbols::read_field<bool>(staticData, bAppExit_field);
		auto errCode = il2cpp_symbols::read_field<int>(staticData, errCode_field);
		if (bAppExit) {
			il2cpp_symbols::write_field(staticData, bAppExit_field, false);
		}

		printf("DMMGameGuardData: isCheck: %d, isInit: %d, bAppExit: %d, errCode: %d\n\n", isCheck, isInit, bAppExit, errCode);
	}

	bool pathed = false;
	bool npPatched = false;
	
	void patchNP(HMODULE module) {
		if (npPatched) return;
		npPatched = true;
		// auto np_module = GetModuleHandle("NPGameDLL");
		if (module) {
			auto SetCallbackToGameMon_addr = GetProcAddress(module, "SetCallbackToGameMon");
			auto PreInitNPGameMonW_addr = GetProcAddress(module, "PreInitNPGameMonW");
			auto PreInitNPGameMonA_addr = GetProcAddress(module, "PreInitNPGameMonA");
			auto InitNPGameMon_addr = GetProcAddress(module, "InitNPGameMon");
			auto SetHwndToGameMon_addr = GetProcAddress(module, "SetHwndToGameMon");

			ADD_HOOK(SetCallbackToGameMon, "SetCallbackToGameMon ap %p");
			ADD_HOOK(PreInitNPGameMonW, "PreInitNPGameMonW ap %p");
			ADD_HOOK(PreInitNPGameMonA, "PreInitNPGameMonA ap %p");
			ADD_HOOK(InitNPGameMon, "InitNPGameMon ap %p");
			ADD_HOOK(SetHwndToGameMon, "SetHwndToGameMon ap %p");
		}
		else {
			printf("NPGameDLL Get failed.\n");
		}
	}

	int retryCount = 0;
	void path_game_assembly()
	{
		if (!mh_inited)
			return;

		auto il2cpp_module = GetModuleHandle("GameAssembly.dll");
		if (!il2cpp_module) {
			printf("GameAssembly.dll not loaded.\n");
			retryCount++;
			if (retryCount == 15) {
				reopen_self();
				return;
			}
		}

		if (pathed) return;
		pathed = true;

		printf("Trying to patch GameAssembly.dll...\n");

		// load il2cpp exported functions
		il2cpp_symbols::init(il2cpp_module);

#pragma region HOOK_ADDRESSES
		
		environment_get_stacktrace = reinterpret_cast<decltype(environment_get_stacktrace)>(
			il2cpp_symbols::get_method_pointer("mscorlib.dll", "System", 
				"Environment", "get_StackTrace", 0)
			);

		DMMGameGuard_klass = il2cpp_symbols::get_class("PRISM.Legacy.dll", "PRISM", "DMMGameGuard");
		isCheck_field = il2cpp_class_get_field_from_name(DMMGameGuard_klass, "_isCheck");
		isInit_field = il2cpp_class_get_field_from_name(DMMGameGuard_klass, "_isInit");
		bAppExit_field = il2cpp_class_get_field_from_name(DMMGameGuard_klass, "_bAppExit");
		errCode_field = il2cpp_class_get_field_from_name(DMMGameGuard_klass, "_errCode");
		initEncoding();
		ReadAllBytes = reinterpret_cast<decltype(ReadAllBytes)>(
			il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.IO", "File", "ReadAllBytes", 1)
			);

		AssetBundle_LoadFromFile = reinterpret_cast<decltype(AssetBundle_LoadFromFile)>(
			il2cpp_symbols::get_method_pointer(
				"UnityEngine.AssetBundleModule.dll", "UnityEngine",
				"AssetBundle", "LoadFromFile", 3
			)
			);
		Object_IsNativeObjectAlive = reinterpret_cast<bool(*)(void*)>(
			il2cpp_symbols::get_method_pointer("UnityEngine.CoreModule.dll", "UnityEngine",
				"Object", "IsNativeObjectAlive", 1)
			);
		AssetBundle_LoadAsset = reinterpret_cast<decltype(AssetBundle_LoadAsset)>(
			il2cpp_resolve_icall("UnityEngine.AssetBundle::LoadAsset_Internal(System.String,System.Type)")
			);
		const auto FontClass = il2cpp_symbols::get_class("UnityEngine.TextRenderingModule.dll", "UnityEngine", "Font");
		Font_Type = il2cpp_type_get_object(il2cpp_class_get_type(FontClass));
		TMP_FontAsset_CreateFontAsset = reinterpret_cast<decltype(TMP_FontAsset_CreateFontAsset)>(il2cpp_symbols::get_method_pointer(
			"Unity.TextMeshPro.dll", "TMPro",
			"TMP_FontAsset", "CreateFontAsset", 1
		));
		TMP_Text_set_font = reinterpret_cast<decltype(TMP_Text_set_font)>(il2cpp_symbols::get_method_pointer(
			"Unity.TextMeshPro.dll", "TMPro",
			"TMP_Text", "set_font", 1
		));
		TMP_Text_get_font = reinterpret_cast<decltype(TMP_Text_get_font)>(il2cpp_symbols::get_method_pointer(
			"Unity.TextMeshPro.dll", "TMPro",
			"TMP_Text", "get_font", 0
		));
		
		const auto TMP_Text_set_text_addr = il2cpp_symbols::get_method_pointer(
			"Unity.TextMeshPro.dll", "TMPro",
			"TMP_Text", "set_text", 1
		);
		const auto UITextMeshProUGUI_Awake_addr = il2cpp_symbols::get_method_pointer(
			"ENTERPRISE.UI.dll", "ENTERPRISE.UI",
			"UITextMeshProUGUI", "Awake", 0
		);

		auto ScenarioManager_Init_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM.Scenario",
			"ScenarioManager", "Init", 1
		);
		
		auto ResorceVersion_get = il2cpp_symbols::get_method_pointer(
			"PRISM.Module.Networking.dll", "PRISM.Module.Networking",
			"get_ResourceVersion", "Init", 1
		);
		
		auto DataFile_GetBytes_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"DataFile", "GetBytes", 1
		);
		DataFile_IsKeyExist = reinterpret_cast<decltype(DataFile_IsKeyExist)>(
			il2cpp_symbols::get_method_pointer(
				"PRISM.Legacy.dll", "PRISM",
				"DataFile", "IsKeyExist", 1
			)
			);

		auto SetResolution_addr = il2cpp_symbols::get_method_pointer(
			"UnityEngine.CoreModule.dll", "UnityEngine",
			"Screen", "SetResolution", 3
		);

		auto UnsafeLoadBytesFromKey_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.ResourceManagement.dll", "PRISM.ResourceManagement",
			"ResourceLoader", "UnsafeLoadBytesFromKey", 2
		);

		auto TextLog_AddLog_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM.Scenario",
			"TextLog", "AddLog", 4
		);

		auto LocalizationManager_GetTextOrNull_addr = il2cpp_symbols::get_method_pointer(
			"ENTERPRISE.Localization.dll", "ENTERPRISE.Localization",
			"LocalizationManager", "GetTextOrNull", 2
		);
		auto get_NeedsLocalization_addr = il2cpp_symbols::get_method_pointer(
			"ENTERPRISE.UI.dll", "ENTERPRISE.UI",
			"UITextMeshProUGUI", "get_NeedsLocalization", 0
		);
		auto LiveMVView_UpdateLyrics_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM.Live",
			"LiveMVView", "UpdateLyrics", 1
		);
		auto TimelineController_SetLyric_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"TimelineController", "SetLyric", 1
		);

		auto get_baseCamera_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"CameraController", "get_baseCamera", 0
		);

		auto Unity_get_pos_injected_addr = il2cpp_resolve_icall("UnityEngine.Transform::get_position_Injected(UnityEngine.Vector3&)");
		auto Unity_set_pos_injected_addr = il2cpp_resolve_icall("UnityEngine.Transform::set_position_Injected(UnityEngine.Vector3&)");

		auto Unity_get_fieldOfView_addr = il2cpp_resolve_icall("UnityEngine.Camera::get_fieldOfView()");
		auto Unity_set_fieldOfView_addr = il2cpp_resolve_icall("UnityEngine.Camera::set_fieldOfView(System.Single)");
		auto Unity_LookAt_Injected_addr = il2cpp_resolve_icall("UnityEngine.Transform::Internal_LookAt_Injected(UnityEngine.Vector3&,UnityEngine.Vector3&)");
		auto Unity_set_nearClipPlane_addr = il2cpp_resolve_icall("UnityEngine.Camera::set_nearClipPlane(System.Single)");
		auto Unity_get_nearClipPlane_addr = il2cpp_resolve_icall("UnityEngine.Camera::get_nearClipPlane()");
		auto Unity_get_farClipPlane_addr = il2cpp_resolve_icall("UnityEngine.Camera::get_farClipPlane()");
		auto Unity_set_farClipPlane_addr = il2cpp_resolve_icall("UnityEngine.Camera::set_farClipPlane(System.Single)");
		auto Unity_get_rotation_Injected_addr = il2cpp_resolve_icall("UnityEngine.Transform::get_rotation_Injected(UnityEngine.Quaternion&)");
		auto Unity_set_rotation_Injected_addr = il2cpp_resolve_icall("UnityEngine.Transform::set_rotation_Injected(UnityEngine.Quaternion&)");

		auto InvokeMoveNext_addr = il2cpp_symbols::get_method_pointer(
			"UnityEngine.CoreModule.dll", "UnityEngine",
			"SetupCoroutine", "InvokeMoveNext", 2
		);
		auto Live_SetEnableDepthOfField_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"LiveScene", "SetEnableDepthOfField", 1
		);
		auto Live_Update_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"LiveScene", "Update", 0
		);
		auto LiveCostumeChangeView_setTryOnMode_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Interactions.Live.dll", "PRISM.Interactions",
			"LiveCostumeChangeView", "_setTryOnMode", 2
		);
		auto LiveCostumeChangeView_setIdolCostume_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Interactions.Live.dll", "PRISM.Interactions",
			"LiveCostumeChangeView", "_setIdolCostume", 3
		);

		LiveCostumeChangeModel_get_Idol = reinterpret_cast<decltype(LiveCostumeChangeModel_get_Idol)>(il2cpp_symbols::get_method_pointer(
			"PRISM.Adapters.dll", "PRISM.Adapters",
			"LiveCostumeChangeModel", "get_Idol", 0
		));

		auto LiveCostumeChangeModel_GetDress_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Adapters.dll", "PRISM.Adapters",
			"LiveCostumeChangeModel", "GetDress", 1
		);
		auto LiveCostumeChangeModel_GetHairstyle_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Adapters.dll", "PRISM.Adapters",
			"LiveCostumeChangeModel", "GetHairstyle", 1
		);
		auto LiveCostumeChangeModel_GetAccessory_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Adapters.dll", "PRISM.Adapters",
			"LiveCostumeChangeModel", "GetAccessory", 1
		);
		auto LiveCostumeChangeModel_ctor_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Adapters.dll", "PRISM.Adapters",
			"LiveCostumeChangeModel", ".ctor", 3
		);

		auto AssembleCharacter_ApplyParam_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"AssembleCharacter", "ApplyParam", 6
		);

		auto MainThreadDispatcher_LateUpdate_addr = il2cpp_symbols::get_method_pointer(
			"UniRx.dll", "UniRx",
			"MainThreadDispatcher", "LateUpdate", 0
		);

		auto LiveMVUnit_GetMemberChangeRequestData_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM.Live",
			"LiveMVUnit", "GetMemberChangeRequestData", 3
		);

		auto PopupSystem_ShowPopup_addr = il2cpp_symbols::get_method_pointer(
			"ENTERPRISE.UI.dll", "ENTERPRISE.Popup",
			"PopupSystem", "ShowPopup", 3
		);

		auto CriWareErrorHandler_HandleMessage_addr = il2cpp_symbols::get_method_pointer(
			"CriMw.CriWare.Runtime.dll", "CriWare",
			"CriWareErrorHandler", "HandleMessage", 1
		);

		auto GGIregualDetector_ShowPopup_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"GGIregualDetector", "ShowPopup", 1
		);
		auto DMMGameGuard_NPGameMonCallback_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"DMMGameGuard", "NPGameMonCallback", 2
		);
		auto DMMGameGuard_Setup_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"DMMGameGuard", "Setup", 0
		);		
		auto DMMGameGuard_SetCheckMode_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"DMMGameGuard", "SetCheckMode", 1
		);
		/*
		CloseNPGameMon = reinterpret_cast<decltype(CloseNPGameMon)>(il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM",
			"DMMGameGuard", "CloseNPGameMon", 0
		));*/

		auto set_fps_addr = il2cpp_resolve_icall("UnityEngine.Application::set_targetFrameRate(System.Int32)");
		auto set_vsync_count_addr = il2cpp_resolve_icall("UnityEngine.QualitySettings::set_vSyncCount(System.Int32)");
		auto Unity_Quit_addr = il2cpp_resolve_icall("UnityEngine.Application::Quit(System.Int32)");


		auto assemblyLoad = reinterpret_cast<void* (*)(Il2CppString*)>(
			il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.Reflection",
				"Assembly", "Load", 1)
			);
		auto assemblyGetType = reinterpret_cast<Il2CppReflectionType * (*)(void*, Il2CppString*)>(
			il2cpp_symbols::get_method_pointer("mscorlib.dll", "System.Reflection",
				"Assembly", "GetType", 1)
			);
		auto reflectionAssembly = assemblyLoad(il2cpp_string_new("mscorlib"));
		auto reflectionType = assemblyGetType(
			reflectionAssembly, il2cpp_string_new("System.Collections.Generic.Dictionary`2[System.Int32, PRISM.Module.Networking.ICostumeStatus]"));
		auto dic_klass = il2cpp_class_from_system_type(reflectionType);
		auto dic_int_ICostumeStatus_add_addr = il2cpp_class_get_method_from_name(dic_klass, "Add", 2)->methodPointer;

		auto LiveMVUnitConfirmationModel_ctor_addr = il2cpp_symbols::get_method_pointer(
			"PRISM.Legacy.dll", "PRISM.Live",
			"LiveMVUnitConfirmationModel", ".ctor", 3
		);

#pragma endregion
		ADD_HOOK(LocalizationManager_GetTextOrNull, "LocalizationManager_GetTextOrNull at %p");
		ADD_HOOK(get_NeedsLocalization, "get_NeedsLocalization at %p");
		ADD_HOOK(LiveMVView_UpdateLyrics, "LiveMVView_UpdateLyrics at %p");
		ADD_HOOK(TimelineController_SetLyric, "TimelineController_SetLyric at %p");
		ADD_HOOK(get_baseCamera, "get_baseCamera at %p");
		ADD_HOOK(Unity_set_pos_injected, "Unity_set_pos_injected at %p");
		ADD_HOOK(Unity_get_pos_injected, "Unity_get_pos_injected at %p");
		ADD_HOOK(Unity_get_fieldOfView, "Unity_get_fieldOfView at %p");
		ADD_HOOK(Unity_set_fieldOfView, "Unity_set_fieldOfView at %p");
		ADD_HOOK(Unity_LookAt_Injected, "Unity_LookAt_Injected at %p");
		ADD_HOOK(Unity_set_nearClipPlane, "Unity_set_nearClipPlane at %p");
		ADD_HOOK(Unity_get_nearClipPlane, "Unity_get_nearClipPlane at %p");
		ADD_HOOK(Unity_get_farClipPlane, "Unity_get_farClipPlane at %p");
		ADD_HOOK(Unity_set_farClipPlane, "Unity_set_farClipPlane at %p");
		ADD_HOOK(Unity_get_rotation_Injected, "Unity_get_rotation_Injected at %p");
		ADD_HOOK(Unity_set_rotation_Injected, "Unity_set_rotation_Injected at %p");

		ADD_HOOK(TMP_Text_set_text, "TMP_Text_set_text at %p");
		ADD_HOOK(UITextMeshProUGUI_Awake, "UITextMeshProUGUI_Awake at %p");
		ADD_HOOK(ScenarioManager_Init, "ScenarioManager_Init at %p");
		ADD_HOOK(DataFile_GetBytes, "DataFile_GetBytes at %p");
		ADD_HOOK(UnsafeLoadBytesFromKey, "UnsafeLoadBytesFromKey at %p");
		ADD_HOOK(TextLog_AddLog, "TextLog_AddLog at %p");
		ADD_HOOK(SetResolution, "SetResolution at %p");
		ADD_HOOK(InvokeMoveNext, "InvokeMoveNext at %p");
		ADD_HOOK(Live_SetEnableDepthOfField, "Live_SetEnableDepthOfField at %p");
		ADD_HOOK(Live_Update, "Live_Update at %p");
		ADD_HOOK(LiveCostumeChangeView_setTryOnMode, "LiveCostumeChangeView_setTryOnMode at %p");
		ADD_HOOK(LiveCostumeChangeView_setIdolCostume, "LiveCostumeChangeView_setIdolCostume at %p");
		ADD_HOOK(LiveCostumeChangeModel_GetDress, "LiveCostumeChangeModel_GetDress at %p");
		ADD_HOOK(LiveCostumeChangeModel_GetHairstyle, "LiveCostumeChangeModel_GetHairstyle at %p");
		ADD_HOOK(LiveCostumeChangeModel_GetAccessory, "LiveCostumeChangeModel_GetAccessory at %p");
		ADD_HOOK(LiveCostumeChangeModel_ctor, "LiveCostumeChangeModel_ctor at %p");
		ADD_HOOK(AssembleCharacter_ApplyParam, "AssembleCharacter_ApplyParam at %p");
		ADD_HOOK(MainThreadDispatcher_LateUpdate, "MainThreadDispatcher_LateUpdate at %p");
		ADD_HOOK(dic_int_ICostumeStatus_add, "dic_int_ICostumeStatus_add at %p");
		ADD_HOOK(LiveMVUnitConfirmationModel_ctor, "LiveMVUnitConfirmationModel_ctor at %p");
		// ADD_HOOK(PopupSystem_ShowPopup, "PopupSystem_ShowPopup at %p");
		ADD_HOOK(LiveMVUnit_GetMemberChangeRequestData, "LiveMVUnit_GetMemberChangeRequestData at %p");
		ADD_HOOK(CriWareErrorHandler_HandleMessage, "CriWareErrorHandler_HandleMessage at %p");
		ADD_HOOK(GGIregualDetector_ShowPopup, "GGIregualDetector_ShowPopup at %p");
		ADD_HOOK(DMMGameGuard_NPGameMonCallback, "DMMGameGuard_NPGameMonCallback at %p");
		// ADD_HOOK(DMMGameGuard_Setup, "DMMGameGuard_Setup at %p");
		ADD_HOOK(DMMGameGuard_SetCheckMode, "DMMGameGuard_SetCheckMode at %p");
		ADD_HOOK(set_fps, "set_fps at %p");
		ADD_HOOK(set_vsync_count, "set_vsync_count at %p");
		ADD_HOOK(Unity_Quit, "Unity_Quit at %p");

		LoadExtraAssetBundle();
		on_hotKey_0 = []() {
			startSCGUI();
			// needPrintStack = !needPrintStack;
		};
		SCCamera::initCameraSettings();
		g_on_hook_ready();
	}
}

void uninit_hook()
{
	if (!mh_inited)
		return;

	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}

bool init_hook()
{
	if (mh_inited)
		return false;

	if (MH_Initialize() != MH_OK)
		return false;

	g_on_close = []() {
		uninit_hook();
		TerminateProcess(GetCurrentProcess(), 0);
	};

	mh_inited = true;

	MH_CreateHook(LoadLibraryW, load_library_w_hook, &load_library_w_orig);
	MH_EnableHook(LoadLibraryW);
	return true;
}
