"""Conan 2 recipe for plotjuggler_sdk.

Exposes three CMake components under the `plotjuggler_sdk::` namespace:

  base         — pj_base, vocabulary types (always available)
  plugin_sdk   — umbrella for plugin authors (base + dialog SDK + parser SDK)
  plugin_host  — umbrella for host loaders (data_source/parser/toolbox/dialog)

A consuming Conan recipe declares e.g. `plotjuggler_sdk/0.13.0` and then:

    find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)
    target_link_libraries(my_plugin PRIVATE plotjuggler_sdk::plugin_sdk)

The `plugin_sdk` component also ships `PjPluginManifest.cmake`, so authors can
call `pj_emit_plugin_manifest()` without copying the helper into their tree.

The columnar storage engine (formerly the `datastore` component) is no longer
part of this SDK package — it now lives in the PlotJuggler application repo,
since plugins reach it only through the C ABI, never by linking it.

Local development (build.sh) uses this same recipe with `with_tests=True` so
gtest is resolved as a test_require.
"""

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from conan.tools.files import copy
import os


class PlotjugglerSdkConan(ConanFile):
    name = "plotjuggler_sdk"
    # UNRELEASED BREAK: 0.13.0 unifies markers + transforms into the single host
    # service `pj.data_processors.v1` via a `kind` discriminator (removed the old
    # `pj.markers.v1` and the interim `pj.generators.v1`; generalized
    # `create_data_processor`/`validate_data_processor_script` with kind/language/flags).
    # This is an ABI/API change — normally MAJOR — but no PUBLIC tag ever shipped
    # `pj.data_processors.v1`, so no released plugin breaks and 0.13.0 stays a valid
    # pre-1.0 step. The FIRST public release that carries the unified
    # `pj.data_processors.v1` MUST be tagged 1.0.0. See CHANGELOG.md.
    version = "0.13.0"
    # Apache-2.0 covers the whole SDK (pj_base + pj_plugins). See LICENSE.
    license = "Apache-2.0"
    url = "https://github.com/PlotJuggler/plotjuggler_sdk"
    description = "C++20 foundation libraries for PlotJuggler: plugin SDK and plugin host loaders."
    topics = ("plotjuggler", "plugin-sdk", "telemetry", "data-visualization")
    package_type = "static-library"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "fPIC": [True, False],
        "with_host": [True, False],
        "with_tests": [True, False],
        "assert_throws": [True, False],
    }
    default_options = {
        "fPIC": True,
        "with_host": True,
        "with_tests": False,
        "assert_throws": False,
        # fmt is an implementation detail. Compile it header-only so the
        # static archives do not export a downstream fmt link dependency.
        "fmt/*:header_only": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "LICENSE-APACHE",
        "cmake/*",
        "pj_base/*",
        "pj_plugins/*",
        "examples/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def requirements(self):
        # nlohmann_json appears in public headers (widget_data, plugin_catalog).
        self.requires("nlohmann_json/3.12.0", transitive_headers=True)

        # fmt is private and header-only at build time; do not propagate it
        # into consumers of this static-library package.
        self.requires(
            "fmt/12.1.0",
            headers=True,
            libs=False,
            visible=False,
            transitive_headers=False,
            transitive_libs=False,
        )

        # fast_float backs the floating-point branch of PJ::parseNumber. It
        # is header-only and stays private: never appears in any public
        # pj_base header, never propagated to downstream consumers.
        self.requires(
            "fast_float/8.1.0",
            headers=True,
            libs=False,
            visible=False,
            transitive_headers=False,
            transitive_libs=False,
        )

    def build_requirements(self):
        # Tests are local-dev only. Consumers of plotjuggler_sdk never see this.
        if self.options.with_tests:
            self.test_requires("gtest/1.17.0")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.cache_variables["PJ_INSTALL_SDK"] = True
        tc.cache_variables["PJ_BUILD_TESTS"] = bool(self.options.with_tests)
        tc.cache_variables["PJ_BUILD_PORTED_PLUGINS"] = False
        tc.cache_variables["PJ_ASSERT_THROWS"] = bool(self.options.assert_throws)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # Ships LICENSE (the per-module map) plus the full Apache-2.0 text
        # (LICENSE-APACHE).
        copy(
            self,
            "LICENSE*",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "plotjuggler_sdk")
        # No top-level umbrella target: the three components have
        # mutually-exclusive audiences. Consumers must request a component.

        # Conan 2's CMakeDeps only aggregates cmake_build_modules declared at
        # the package level (self.cpp_info), not at component level — declaring
        # it on the `sdk` component below silently produced an empty
        # plotjuggler_sdk_BUILD_MODULES_PATHS_RELEASE in the generated data
        # file. Ship the PjPluginManifest helper from the package root so
        # CMakeDeps actually include()s it after find_package() returns.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("lib", "cmake", "plotjuggler_sdk", "PjPluginManifest.cmake"),
        ])

        # --- base ---
        base = self.cpp_info.components["base"]
        base.set_property("cmake_target_name", "plotjuggler_sdk::base")
        base.libs = ["pj_base"]
        base.includedirs = ["include"]

        # --- plugin_sdk (umbrella INTERFACE: pj_base + pj_dialog_sdk) ---
        sdk = self.cpp_info.components["plugin_sdk"]
        sdk.set_property("cmake_target_name", "plotjuggler_sdk::plugin_sdk")
        sdk.libs = []  # INTERFACE only
        sdk.includedirs = ["include"]
        sdk.requires = ["base", "nlohmann_json::nlohmann_json"]

        # --- plugin_host (umbrella linking every host-side loader) ---
        if self.options.with_host:
            host = self.cpp_info.components["plugin_host"]
            host.set_property("cmake_target_name", "plotjuggler_sdk::plugin_host")
            host.libs = [
                "pj_plugin_runtime_catalog",
                "pj_data_source_host",
                "pj_message_parser_host",
                "pj_toolbox_host",
                "pj_dialog_library",
                "pj_plugin_catalog",
                "pj_plugin_loader_detail",
            ]
            host.includedirs = ["include"]
            host.requires = ["base", "nlohmann_json::nlohmann_json"]
            if self.settings.os in ("Linux", "FreeBSD"):
                host.system_libs = ["dl"]
