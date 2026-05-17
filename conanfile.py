"""Conan 2 recipe for plotjuggler_core.

Exposes four CMake components under the `plotjuggler_core::` namespace:

  base         — pj_base, vocabulary types (always available)
  datastore    — pj_datastore, columnar engine (option: with_datastore)
  plugin_sdk   — umbrella for plugin authors (base + dialog SDK + parser SDK)
  plugin_host  — umbrella for host loaders (data_source/parser/toolbox/dialog)

A consuming Conan recipe declares e.g. `plotjuggler_core/0.1.0` and then:

    find_package(plotjuggler_core REQUIRED COMPONENTS plugin_sdk)
    target_link_libraries(my_plugin PRIVATE plotjuggler_core::plugin_sdk)

The `plugin_sdk` component also ships `PjPluginManifest.cmake`, so authors can
call `pj_emit_plugin_manifest()` without copying the helper into their tree.

Local development (build.sh) uses this same recipe with `with_tests=True` so
gtest/benchmark/arrow are resolved as test_requires.
"""

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from conan.tools.files import copy
import os


class PlotjugglerCoreConan(ConanFile):
    name = "plotjuggler_core"
    version = "0.1.0"
    license = "MIT"
    url = "https://github.com/PlotJuggler/plotjuggler_core"
    description = "C++20 foundation libraries for PlotJuggler: storage engine, plugin SDK, plugin host loaders."
    topics = ("plotjuggler", "plugin-sdk", "telemetry", "data-visualization")
    package_type = "static-library"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "fPIC": [True, False],
        "with_datastore": [True, False],
        "with_host": [True, False],
        "with_tests": [True, False],
        "with_parquet_example": [True, False],
        "assert_throws": [True, False],
    }
    default_options = {
        "fPIC": True,
        "with_datastore": True,
        "with_host": True,
        "with_tests": False,
        "with_parquet_example": False,
        "assert_throws": False,
        # Arrow build flags (only resolved when with_parquet_example=True).
        "arrow/*:parquet": True,
        "arrow/*:with_snappy": True,
        # pj_datastore's Arrow IPC import path needs nanoarrow_ipc + flatcc.
        "nanoarrow/*:with_ipc": True,
        # Boost is pulled in transitively by arrow. without_cobalt avoids a
        # known upstream packaging error in recent boost recipes; without_test
        # trims unneeded modules.
        "boost/*:without_test": True,
        "boost/*:without_cobalt": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "cmake/*",
        "pj_base/*",
        "pj_datastore/*",
        "pj_plugins/*",
        "examples/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def requirements(self):
        # nlohmann_json appears in public headers (widget_data, plugin_catalog).
        self.requires("nlohmann_json/3.12.0", transitive_headers=True)

        if self.options.with_datastore:
            # fmt + tsl-robin-map are private; nanoarrow is in public headers.
            self.requires("fmt/12.1.0")
            self.requires("tsl-robin-map/1.4.0", transitive_headers=True)
            self.requires("nanoarrow/0.7.0", transitive_headers=True)

    def build_requirements(self):
        # Tests + benchmarks + parquet example are local-dev only. Consumers
        # of plotjuggler_core never see these.
        if self.options.with_tests:
            self.test_requires("gtest/1.17.0")
            if self.options.with_datastore:
                self.test_requires("benchmark/1.9.4")
        if self.options.with_parquet_example:
            self.test_requires("arrow/23.0.1")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.cache_variables["PJ_INSTALL_SDK"] = True
        tc.cache_variables["PJ_BUILD_DATASTORE"] = bool(self.options.with_datastore)
        tc.cache_variables["PJ_BUILD_TESTS"] = bool(self.options.with_tests)
        tc.cache_variables["PJ_BUILD_PORTED_PLUGINS"] = False
        tc.cache_variables["PJ_BUILD_PARQUET_IMPORT_EXAMPLE"] = bool(
            self.options.with_parquet_example
        )
        tc.cache_variables["PJ_ASSERT_THROWS"] = bool(self.options.assert_throws)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "plotjuggler_core")
        # No top-level umbrella target: the four components have
        # mutually-exclusive audiences. Consumers must request a component.

        # --- base ---
        base = self.cpp_info.components["base"]
        base.set_property("cmake_target_name", "plotjuggler_core::base")
        base.libs = ["pj_base"]
        base.includedirs = ["include"]

        # --- datastore (optional) ---
        if self.options.with_datastore:
            ds = self.cpp_info.components["datastore"]
            ds.set_property("cmake_target_name", "plotjuggler_core::datastore")
            ds.libs = ["pj_datastore"]
            ds.includedirs = ["include"]
            ds.requires = [
                "base",
                "fmt::fmt",
                "tsl-robin-map::tsl-robin-map",
                "nanoarrow::nanoarrow",
            ]

        # --- plugin_sdk (umbrella INTERFACE: pj_base + pj_dialog_sdk) ---
        sdk = self.cpp_info.components["plugin_sdk"]
        sdk.set_property("cmake_target_name", "plotjuggler_core::plugin_sdk")
        sdk.libs = []  # INTERFACE only
        sdk.includedirs = ["include"]
        sdk.requires = ["base", "nlohmann_json::nlohmann_json"]
        # Ship the cmake helper alongside the component. cmake_build_modules
        # makes Conan auto-include() it after find_package() succeeds.
        sdk.set_property("cmake_build_modules", [
            os.path.join("lib", "cmake", "plotjuggler_core", "PjPluginManifest.cmake"),
        ])

        # --- plugin_host (umbrella linking every host-side loader) ---
        if self.options.with_host:
            host = self.cpp_info.components["plugin_host"]
            host.set_property("cmake_target_name", "plotjuggler_core::plugin_host")
            host.libs = [
                "pj_plugin_runtime_catalog",
                "pj_data_source_host",
                "pj_message_parser_host",
                "pj_toolbox_host",
                "pj_dialog_library",
                "pj_plugin_catalog",
            ]
            host.includedirs = ["include"]
            host.requires = ["base", "nlohmann_json::nlohmann_json"]
            if self.settings.os in ("Linux", "FreeBSD"):
                host.system_libs = ["dl"]
