# GNU Make on Windows. Default: MinGW-w64 x64. For MSVC, use an x64 Developer Prompt.
.DEFAULT_GOAL := all
SHELL := cmd.exe
.SHELLFLAGS := /d /s /c
.DELETE_ON_ERROR:

TOOLCHAIN ?= mingw
POWERSHELL ?= powershell.exe
HEADERS := $(wildcard src/*.h)
UNIT_TESTS := history_test hotkey_test catalog_test
UI_TESTS := settings_render_test
INTEGRATION_TESTS := integration_test settings_test hotkey_integration_test flyout_test

ifeq ($(TOOLCHAIN),mingw)
BUILD_DIR := build
ifneq ($(filter default undefined,$(origin CXX)),)
CXX := g++
endif
WINDRES ?= windres
CPPFLAGS += -DUNICODE -D_UNICODE -DNOMINMAX
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers
LDFLAGS += -static
OBJEXT := o
RESEXT := o
else ifeq ($(TOOLCHAIN),msvc)
# Separate outputs prevent mixing objects and binaries from different toolchains.
BUILD_DIR := build/msvc
ifneq ($(filter default undefined,$(origin CXX)),)
CXX := cl
endif
RC := rc
CPPFLAGS += /DUNICODE /D_UNICODE /DNOMINMAX
CXXFLAGS += /nologo /std:c++17 /W4 /WX /EHsc /O2 /MT /utf-8
LDFLAGS += /INCREMENTAL:NO
OBJEXT := obj
RESEXT := res
else
$(error Unknown TOOLCHAIN '$(TOOLCHAIN)'; use mingw or msvc)
endif

OBJ_DIR := $(BUILD_DIR)/obj
APP := $(BUILD_DIR)/LayoutDock.exe
WIDGET := $(BUILD_DIR)/LayoutDock.Widget.dll
RESOURCES := $(OBJ_DIR)/resources.$(RESEXT)
SETTINGS_OBJECTS := $(OBJ_DIR)/settings.$(OBJEXT) $(OBJ_DIR)/settings_flyout.$(OBJEXT)
APP_OBJECTS := $(OBJ_DIR)/controller.$(OBJEXT) $(SETTINGS_OBJECTS)
TEST_BINARIES := $(addprefix $(BUILD_DIR)/,$(addsuffix .exe,$(UNIT_TESTS) $(UI_TESTS) $(INTEGRATION_TESTS)))
APP_LIBS := user32 gdi32 shell32 advapi32 ole32 oleaut32 uiautomationcore comctl32 dwmapi gdiplus uuid
WIDGET_LIBS := user32 gdi32 advapi32 d2d1 dwrite dcomp d3d11 dxgi
TEST_LIBS := user32 gdi32 shell32 advapi32 comctl32 dwmapi gdiplus

.PHONY: all tests test test-ui run stop clean help
all: $(APP) $(WIDGET)
tests: $(TEST_BINARIES)

# Unit tests are safe to run alongside normal desktop work.
test: $(addprefix $(BUILD_DIR)/,$(addsuffix .exe,$(UNIT_TESTS)))
	"$(BUILD_DIR)/history_test.exe"
	"$(BUILD_DIR)/hotkey_test.exe"
	"$(BUILD_DIR)/catalog_test.exe"

# Opens a temporary settings window with an in-memory model; no running app required.
test-ui: $(BUILD_DIR)/settings_render_test.exe
	"$(BUILD_DIR)/settings_render_test.exe"

run: all
	$(POWERSHELL) -NoProfile -Command "Start-Process -FilePath '$(APP)' -WindowStyle Hidden"

stop:
	$(POWERSHELL) -NoProfile -Command "if (Test-Path -LiteralPath '$(APP)') { Start-Process -FilePath '$(APP)' -ArgumentList '--stop' -WindowStyle Hidden -Wait }"

clean:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/clean.ps1

help:
	@echo make                 Build LayoutDock.exe and LayoutDock.Widget.dll
	@echo make tests           Build all test executables
	@echo make test            Run unit tests
	@echo make test-ui         Run the isolated settings rendering test
	@echo make run / make stop Start or stop LayoutDock
	@echo make clean           Remove generated build files; stop LayoutDock first
	@echo make TOOLCHAIN=msvc  Use MSVC from an x64 Developer Prompt; outputs: build/msvc

$(BUILD_DIR):
	@if not exist "$@" mkdir "$@"

$(OBJ_DIR): | $(BUILD_DIR)
	@if not exist "$@" mkdir "$@"

ifeq ($(TOOLCHAIN),mingw)
$(OBJ_DIR)/%.$(OBJEXT): src/%.cpp Makefile | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(OBJ_DIR)/%.$(OBJEXT): tests/%.cpp Makefile | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(RESOURCES): src/resources.rc src/settings.rc src/settings_ids.h src/resource_ids.h src/app_info.h src/app.manifest assets/layoutdock.ico Makefile | $(OBJ_DIR)
	$(WINDRES) -I src "$<" -O coff -o "$@"

$(APP): $(APP_OBJECTS) $(RESOURCES) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -municode -mwindows $^ -o "$@" $(addprefix -l,$(APP_LIBS))

$(WIDGET): $(OBJ_DIR)/widget.$(OBJEXT) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -shared $^ -o "$@" $(addprefix -l,$(WIDGET_LIBS))

$(BUILD_DIR)/settings_render_test.exe: $(OBJ_DIR)/settings_render_test.$(OBJEXT) $(SETTINGS_OBJECTS) $(RESOURCES) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) $^ -o "$@" $(addprefix -l,$(TEST_LIBS))

$(BUILD_DIR)/%.exe: $(OBJ_DIR)/%.$(OBJEXT) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) $^ -o "$@" $(addprefix -l,$(TEST_LIBS))

-include $(wildcard $(OBJ_DIR)/*.d)
else
$(OBJ_DIR)/%.$(OBJEXT): src/%.cpp $(HEADERS) Makefile | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) /c "$<" /Fo:"$@"

$(OBJ_DIR)/%.$(OBJEXT): tests/%.cpp $(HEADERS) Makefile | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) /c "$<" /Fo:"$@"

$(RESOURCES): src/resources.rc src/settings.rc src/settings_ids.h src/resource_ids.h src/app_info.h src/app.manifest assets/layoutdock.ico Makefile | $(OBJ_DIR)
	$(RC) /nologo /I src /fo "$@" "$<"

$(APP): $(APP_OBJECTS) $(RESOURCES) | $(BUILD_DIR)
	$(CXX) /nologo $^ /Fe:"$@" /link $(LDFLAGS) /SUBSYSTEM:WINDOWS /MANIFEST:NO $(addsuffix .lib,$(APP_LIBS))

$(WIDGET): $(OBJ_DIR)/widget.$(OBJEXT) | $(BUILD_DIR)
	$(CXX) /nologo /LD $^ /Fe:"$@" /link $(LDFLAGS) /IMPLIB:"$(BUILD_DIR)/LayoutDock.Widget.lib" $(addsuffix .lib,$(WIDGET_LIBS))

$(BUILD_DIR)/settings_render_test.exe: $(OBJ_DIR)/settings_render_test.$(OBJEXT) $(SETTINGS_OBJECTS) $(RESOURCES) | $(BUILD_DIR)
	$(CXX) /nologo $^ /Fe:"$@" /link $(LDFLAGS) /MANIFEST:NO $(addsuffix .lib,$(TEST_LIBS))

$(BUILD_DIR)/%.exe: $(OBJ_DIR)/%.$(OBJEXT) | $(BUILD_DIR)
	$(CXX) /nologo $^ /Fe:"$@" /link $(LDFLAGS) $(addsuffix .lib,$(TEST_LIBS))
endif

# Keep test objects for incremental builds instead of treating them as intermediates.
.SECONDARY:
