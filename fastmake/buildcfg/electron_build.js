import { constVal, buildCommonSetting, applyMacBuildSettings } from "./const_val.js";

var json = [{
	"var":[
		{"sdkPath":constVal.sdkPath},
		{"clangPath":constVal.clangPath},
		{"srcPath":constVal.srcPath},
		{"ndkIncludePath":constVal.ndkIncludePath},
		{"ndkBinPath":constVal.ndkBinPath},
		{"v8dir": constVal.v8dir},
		{"targetDir": constVal.targetDir},
		{"sysroot": constVal.sysroot},
	],
	"compile":{
		"ccompiler":"${clangPath}/clang.exe",
		"cppcompiler":"${clangPath}/clang++.exe",
			
		"include":[
			"${sdkPath}/include/c++/7.2.0",
			"${sdkPath}/include/c++/7.2.0/include",
			//"C:/cygwin64/usr/local/x86_64-unknown-linux-gnu/lib/gcc/x86_64-unknown-linux-gnu/7.2.0/include", // sse is compile error
			"${ndkIncludePath}",
			"${sdkPath}/include/c++/7.2.0/x86_64-unknown-linux-gnu/",
			"${srcPath}",
			"${sdkPath}/sysroot/usr/include",
			"${sdkPath}/sysroot/usr/",
			"${sdkPath}/sysroot/usr/include/linux",
			"${sysroot}/usr/include/cairo",
			"${sysroot}/usr/include/glib-2.0",
			"${sysroot}/usr/include/gtk-3.0",
			"${sysroot}/usr/include/gdk-pixbuf-2.0",
			"${sysroot}/usr/include/atk-1.0",
			"${sysroot}/usr/include/pango-1.0",
			"${sysroot}/usr/lib/x86_64-linux-gnu/glib-2.0/include",
			"${srcPath}/linux",
			"${srcPath}/gen/${v8dir}/include",
			"${srcPath}/${v8dir}",
			"${srcPath}/${v8dir}/include",
			"${srcPath}/electron",
			"${srcPath}/third_party/skia/include/core",
			"${srcPath}/third_party/skia/include/config",
			"${srcPath}/third_party/skia/include/codec",
			"${srcPath}/third_party/skia/include/device",
			"${srcPath}/third_party/skia/include/effects",
			"${srcPath}/third_party/skia/include/images",
			"${srcPath}/third_party/skia/include/pathops",
			"${srcPath}/third_party/skia/include/ports",
			"${srcPath}/third_party/skia/include/private",
			"${srcPath}/third_party/skia/include/utils",
			"${srcPath}/third_party/skia/include/gpu",
			"${srcPath}/third_party/",
			"${srcPath}/third_party/zlib",
			"${srcPath}/node/openssl/openssl/include",
			"${srcPath}/node/openssl",
			"${srcPath}/node/uv/src",
			"${srcPath}/node/cares/include",
			"${srcPath}/node/cares/config/linux",
			"${srcPath}/node/uv/include"
		],
		"prebuildSrc":[
			//"${srcPath}/electron/common/asar/ScopedTemporaryFile.cpp",
			//"${srcPath}/electron/browser/api/ApiSession.cpp",
			//"${srcPath}/electron/common/asar/Archive.cpp",
			//"${srcPath}/electron/common/api/ApiAsar.cpp",
			"${srcPath}/electron/browser/api/ApiWebContents.cpp",
			"${srcPath}/electron/browser/api/ApiBrowserWindow.cpp",
			"${srcPath}/electron/browser/api/ApiApp.cpp",
			//"${srcPath}/electron/renderer/api/ApiRendererIpc.cpp",
			//"${srcPath}/electron/common/NodeThread.cpp",
			//"${srcPath}/electron/common/NodeBinding.cpp",
			//"${srcPath}/electron/common/ThreadCallWrap.cpp",
			"${srcPath}/electron/common/gin_helper/arguments.cpp",
			"${srcPath}/electron/common/gin_helper/converter.cpp",
			"${srcPath}/electron/common/gin_helper/dictionary.cpp",
			"${srcPath}/electron/common/gin_helper/function_template.cpp",
			"${srcPath}/electron/common/gin_helper/interceptor.cpp",
			"${srcPath}/electron/common/gin_helper/object_template_builder.cpp",
			"${srcPath}/electron/common/gin_helper/per_isolate_data.cpp",
			"${srcPath}/electron/common/gin_helper/wrappable.cpp",
			"${srcPath}/electron/common/api/ApiScreen.cpp",
			"${srcPath}/electron/browser/api/ApiDownloadItem.cpp",
			"${srcPath}/electron/Electron.cpp"
		],
		"src":[
			"${srcPath}/electron/renderer/api/ApiContextBridge.cpp",
			"${srcPath}/electron/browser/api/ApiWebContents.cpp",
			"${srcPath}/electron/browser/api/WindowList.cpp",
			"${srcPath}/electron/browser/api/ApiApp.cpp",
			"${srcPath}/electron/common/OptionsSwitches.cpp",
			"${srcPath}/electron/browser/api/ApiElectron.cpp",
			"${srcPath}/electron/browser/api/ApiMenu.cpp",
			"${srcPath}/electron/browser/api/ApiNativeTheme.mm",
			"${srcPath}/electron/common/NodeThread.cpp",
			"${srcPath}/electron/common/NodeBinding.cpp",
			"${srcPath}/electron/common/AtomCommandLine.cpp",
			"${srcPath}/electron/renderer/api/ApiRendererIpc.cpp",
			"${srcPath}/electron/common/IdLiveDetect.cpp",
			"${srcPath}/electron/common/api/EventEmitter.cpp",
			"${srcPath}/electron/common/api/EventEmitterCaller.cpp",
			"${srcPath}/electron/common/api/Event.cpp",
			"${srcPath}/electron/common/gin_helper/arguments.cpp",
			"${srcPath}/electron/common/gin_helper/converter.cpp",
			"${srcPath}/electron/common/gin_helper/dictionary.cpp",
			"${srcPath}/electron/common/gin_helper/function_template.cpp",
			"${srcPath}/electron/common/gin_helper/interceptor.cpp",
			"${srcPath}/electron/common/gin_helper/object_template_builder.cpp",
			"${srcPath}/electron/common/gin_helper/per_isolate_data.cpp",
			"${srcPath}/electron/common/gin_helper/wrappable.cpp",
			"${srcPath}/electron/common/api/RemoteCallbackFreer.cpp",
			"${srcPath}/electron/common/api/RemoteObjectFreer.cpp",
			"${srcPath}/electron/common/api/ObjectLifeMonitor.cpp",
			"${srcPath}/electron/common/api/ApiV8Util.cpp",
			//"${srcPath}/electron/common/api/ApiShell.cpp",
			//"${srcPath}/electron/common/PlatformUtilWin.cpp",
			"${srcPath}/electron/common/api/ApiOriginalFs.cpp",
			"${srcPath}/electron/common/api/ApiScreen.cpp",
			"${srcPath}/electron/renderer/api/ApiWebFrame.cpp",
			"${srcPath}/electron/Electron.cpp",
			"${srcPath}/electron/common/api/ApiIntlCollator.cpp",
			//"${srcPath}/electron/browser/api/ApiDialag.cpp",
			"${srcPath}/electron/common/api/ApiAsar.cpp",
			"${srcPath}/electron/common/asar/AsarUtil.cpp",
			"${srcPath}/electron/common/asar/Archive.cpp",
			"${srcPath}/electron/common/asar/ScopedTemporaryFile.cpp",
			"${srcPath}/electron/browser/api/ApiProtocol.cpp",
			//"${srcPath}/electron/browser/api/ApiTray.cpp",
			//"${srcPath}/electron/common/SystemTray.cpp",
			//"${srcPath}/content/ui/WebDropSource.cpp",
			//"${srcPath}/content/ui/ClipboardUtil.cpp",
			//"${srcPath}/electron/common/api/ApiNativeImage.cpp",
			//"${srcPath}/electron/renderer/WebviewPluginImpl.cpp",
			//"${srcPath}/electron/renderer/WebviewPlugin.cpp",
			//"${srcPath}/electron/common/InitGdiPlus.cpp",
			//"${srcPath}/electron/common/api/ApiClipboard.cpp",
			//"${srcPath}/content/ui/WCDataObject.cpp",
			//"${srcPath}/electron/NapiStub.cpp",
			"${srcPath}/electron/browser/api/ApiBrowserView.cpp",
			"${srcPath}/electron/browser/api/ApiBrowserWindow.cpp",
			"${srcPath}/electron/common/ThreadCallWrap.cpp",
			"${srcPath}/electron/browser/api/ApiSession.cpp",
			"${srcPath}/electron/browser/api/ApiWebRequest.cpp",
			"${srcPath}/electron/renderer/api/ObjectCache.cpp",
			"${srcPath}/gin/promise.cc",
			"${srcPath}/gin/microtasks_scope.cc",
			"${srcPath}/electron/browser/api/ApiDownloadItem.cpp"

		],
		// 
		"cmd":[
			//"--target=x86_64-linux-guneabi", 
			"-std=c++20",
			"-fno-exceptions",
			"-fms-extensions",
			//"-fshort-wchar",
			"-D__POSIX__=1",
			"-DHAVE_CONFIG_H",
			"-D_GNU_SOURCE",
			"-DUSE_AURA",
			"-DOS_LINUX_FOR_WIN",
			"-DINSIDE_BLINK",
			"-DBLINK_IMPLEMENTATION",
			"-DENABLE_WKE",
			"-D_HAS_CONSTEXPR=0",
			"-D_CRT_SECURE_NO_WARNINGS",
			"-DNODE_WANT_INTERNALS",
			"-DHAVE_OPENSSL",
			"-DCARES_STATICLIB",
			"-DBUILDING_UV_SHARED",
			"-DSK_SUPPORT_LEGACY_CREATESHADER_PTR=1",
			"-DSK_SUPPORT_LEGACY_TYPEFACE_PTR=1",
			"-DENABLE_WKE=1",
			"-DENABLE_MB=1",
			"-DENABLE_NODEJS=1"
		],
		"objdir":"${srcPath}/out/tmp/electron/${targetDir}",
		"outdir":"${srcPath}/out/${targetDir}",
		"target":"libelectron.a",
		"beginLibs":[
		],
		"linkerCmd":[],
		"endLibs":[
		],
		"linker":constVal.linker//"${ndkBinPath}/ar.exe"
	}
}];

if (constVal.isMac) {
	json[0].compile.include.push("${srcPath}/mac");
	json[0].compile.include.push("${srcPath}/gen");
	json[0].compile.include.push("${srcPath}/gen/v8/include");
	json[0].compile.include.push("${srcPath}/v8/include");
	json[0].compile.include.push("${srcPath}/third_party/libnode/src");
	json[0].compile.include.push("${srcPath}/third_party/libuv/include");
	json[0].compile.include.push("${srcPath}/third_party/abseil-cpp");
	json[0].compile.include.push("${srcPath}/third_party/skia");
	json[0].compile.include.push("${srcPath}/base/allocator/partition_allocator/src");
	json[0].compile.include.push("${srcPath}/gen/base/allocator/partition_allocator/src");
	applyMacBuildSettings(json, { v8: true });
	json[0].compile.include.push("${srcPath}/content");
	json[0].compile.include.push("${srcPath}/linux");
	json[0].compile.cmd.push("-DV8_HAVE_TARGET_OS");
	[
		"-DNODE_WANT_INTERNALS=1",
		"-DENABLE_NODEJS=1",
		"-D__POSIX__=1",
		"-DHAVE_OPENSSL=0",
		"-DHAVE_INSPECTOR=0",
		"-DNODE_USE_V8_PLATFORM=1",
		"-DNODE_ARCH=\\\"arm64\\\"",
		"-DNODE_PLATFORM=\\\"darwin\\\"",
		"-DV8_TYPED_ARRAY_MAX_SIZE_IN_HEAP=0",
		"-DV8_ARRAY_BUFFER_INTERNAL_FIELD_COUNT=0",
		"-DV8_ARRAY_BUFFER_VIEW_INTERNAL_FIELD_COUNT=0",
		"-DV8_PROMISE_INTERNAL_FIELD_COUNT=0",
		"-DV8_INTL_SUPPORT",
		"-DV8_USE_EXTERNAL_STARTUP_DATA",
		"-DV8_ATOMIC_OBJECT_FIELD_WRITES",
		"-DV8_ENABLE_LAZY_SOURCE_POSITIONS",
		"-DV8_SHARED_RO_HEAP",
		"-DV8_ENABLE_REGEXP_INTERPRETER_THREADED_DISPATCH",
		"-DV8_SHORT_BUILTIN_CALLS",
		"-DV8_EXTERNAL_CODE_SPACE",
		"-DV8_ENABLE_SYSTEM_INSTRUMENTATION",
		"-DV8_ENABLE_ETW_STACK_WALKING",
		"-DV8_ENABLE_WEBASSEMBLY",
		"-DV8_ENABLE_SPARKPLUG",
		"-DV8_ALLOCATION_FOLDING",
		"-DV8_ALLOCATION_SITE_TRACKING",
		"-DV8_ADVANCED_BIGINT_ALGORITHMS",
		"-DV8_USE_ZLIB",
		"-DV8_COMPRESS_POINTERS",
		"-DV8_COMPRESS_POINTERS_IN_SHARED_CAGE",
		"-DV8_31BIT_SMIS_ON_64BIT_ARCH",
		"-DCPPGC_CAGED_HEAP",
		"-DCPPGC_YOUNG_GENERATION",
		"-DCPPGC_POINTER_COMPRESSION",
	].forEach((arg) => {
		if (json[0].compile.cmd.indexOf(arg) < 0)
			json[0].compile.cmd.push(arg);
	});
	json[0].compile.cmd.push("-Dnode_module_register=electronMacNodeBridgeRegisterModule");

	const macPowerMonitorSrc = "${srcPath}/electron/browser/api/ApiPowerMonitor.cpp";
	const macPowerSaveBlockerSrc = "${srcPath}/electron/browser/api/ApiPowerSaveBlocker.cpp";
	const macGlobalShortcutSrc = "${srcPath}/electron/browser/api/ApiGlobalShortcut.cpp";
	const macAppSrc = "${srcPath}/electron/browser/api/ApiAppMac.cpp";
	const macMenuSrc = "${srcPath}/electron/browser/api/ApiMenu.cpp";
	const macDialogSrc = "${srcPath}/electron/browser/api/ApiDialogMac.cpp";
	const macTraySrc = "${srcPath}/electron/browser/api/ApiTray.cpp";
	const macSystemTraySrc = "${srcPath}/electron/common/SystemTray.cpp";
	const macNativeImageSrc = "${srcPath}/electron/common/api/ApiNativeImage.cpp";
	const macClipboardSrc = "${srcPath}/electron/common/api/ApiClipboard.cpp";
	const macShellSrc = "${srcPath}/electron/common/api/ApiShell.cpp";
	const macWindowListSrc = "${srcPath}/electron/browser/api/WindowList.cpp";
	const macPowerMonitorIdleSrc = [
		"${srcPath}/ui/base/idle/idle.cc",
		"${srcPath}/ui/base/idle/idle_internal.cc",
		"${srcPath}/ui/base/idle/idle_mac.mm",
	];
	const macElectronSupportSrc = [
	];
	const macElectronLinkedBindingSrc = new Set([
		macAppSrc,
		"${srcPath}/electron/browser/api/ApiElectron.cpp",
		macMenuSrc,
		macDialogSrc,
		macTraySrc,
		macSystemTraySrc,
		"${srcPath}/electron/browser/api/ApiNativeTheme.mm",
		macGlobalShortcutSrc,
		macNativeImageSrc,
		macClipboardSrc,
		macShellSrc,
		macPowerMonitorSrc,
		macPowerSaveBlockerSrc,
		"${srcPath}/electron/browser/api/ApiProtocol.cpp",
		macWindowListSrc,
		"${srcPath}/electron/common/AtomCommandLine.cpp",
		"${srcPath}/electron/common/IdLiveDetect.cpp",
		"${srcPath}/electron/common/OptionsSwitches.cpp",
		"${srcPath}/electron/common/api/ApiIntlCollator.cpp",
		"${srcPath}/electron/common/api/ApiOriginalFs.cpp",
		"${srcPath}/electron/common/api/ApiScreen.cpp",
		"${srcPath}/electron/common/api/ApiV8Util.cpp",
		"${srcPath}/electron/common/api/Event.cpp",
		"${srcPath}/electron/common/api/EventEmitter.cpp",
		"${srcPath}/electron/common/api/EventEmitterCaller.cpp",
		"${srcPath}/electron/common/gin_helper/arguments.cpp",
		"${srcPath}/electron/common/gin_helper/converter.cpp",
		"${srcPath}/electron/common/gin_helper/dictionary.cpp",
		"${srcPath}/electron/common/gin_helper/function_template.cpp",
		"${srcPath}/electron/common/gin_helper/interceptor.cpp",
		"${srcPath}/electron/common/gin_helper/object_template_builder.cpp",
		"${srcPath}/electron/common/gin_helper/per_isolate_data.cpp",
		"${srcPath}/electron/common/gin_helper/wrappable.cpp",
		"${srcPath}/electron/common/api/ObjectLifeMonitor.cpp",
		"${srcPath}/electron/common/api/RemoteCallbackFreer.cpp",
		"${srcPath}/electron/common/api/RemoteObjectFreer.cpp",
		"${srcPath}/electron/renderer/api/ApiRendererIpc.cpp",
		"${srcPath}/electron/renderer/api/ObjectCache.cpp",
		...macElectronSupportSrc,
		...macPowerMonitorIdleSrc,
	]);
	json[0].compile.src = json[0].compile.src.filter(src => macElectronLinkedBindingSrc.has(src));
	json[0].compile.src.push(macAppSrc);
	json[0].compile.src.push(macGlobalShortcutSrc);
	json[0].compile.src.push(macMenuSrc);
	json[0].compile.src.push(macDialogSrc);
	json[0].compile.src.push(macTraySrc);
	json[0].compile.src.push(macSystemTraySrc);
	json[0].compile.src.push(macNativeImageSrc);
	json[0].compile.src.push(macClipboardSrc);
	json[0].compile.src.push(macShellSrc);
	json[0].compile.src.push(macPowerMonitorSrc);
	json[0].compile.src.push(macPowerSaveBlockerSrc);
	json[0].compile.src.push(macWindowListSrc);
	json[0].compile.src.push(...macElectronSupportSrc);
	json[0].compile.src.push(...macPowerMonitorIdleSrc);
	json[0].compile.src = [...new Set(json[0].compile.src)];
	json[0].compile.prebuildSrc = json[0].compile.prebuildSrc.filter(src => macElectronLinkedBindingSrc.has(src));
	json[0].compile.prebuildSrc.push(macAppSrc);
	json[0].compile.prebuildSrc.push(macGlobalShortcutSrc);
	json[0].compile.prebuildSrc.push(macMenuSrc);
	json[0].compile.prebuildSrc.push(macDialogSrc);
	json[0].compile.prebuildSrc.push(macTraySrc);
	json[0].compile.prebuildSrc.push(macSystemTraySrc);
	json[0].compile.prebuildSrc.push(macNativeImageSrc);
	json[0].compile.prebuildSrc.push(macClipboardSrc);
	json[0].compile.prebuildSrc.push(macShellSrc);
	json[0].compile.prebuildSrc.push(macPowerMonitorSrc);
	json[0].compile.prebuildSrc.push(macPowerSaveBlockerSrc);
	json[0].compile.prebuildSrc.push(macWindowListSrc);
	json[0].compile.prebuildSrc.push(...macElectronSupportSrc);
	json[0].compile.prebuildSrc.push(...macPowerMonitorIdleSrc);
	json[0].compile.prebuildSrc = [...new Set(json[0].compile.prebuildSrc)];
}

buildCommonSetting(json);
