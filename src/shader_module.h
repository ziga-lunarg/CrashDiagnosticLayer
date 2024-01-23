/*
 Copyright 2018 Google Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

#include <vulkan/vulkan.h>

#include <spirv.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "object_name_db.h"

namespace crash_diagnostic_layer {

class CdlContext;
class Device;

class ShaderModule {
   public:
    struct LoadOptions {
        enum Mode {
            kNone = 0,               // ignore code, only parse for names
            kDumpOnCreate = 1 << 0,  // dump SPRIV code on creation
            kKeepInMemory = 1 << 1,  // store a copy of SPRIV to dump later
        };
    };

    ShaderModule(CdlContext* p_cdl, VkShaderModule vk_shader_module, int load_options, size_t code_size,
                 const char* p_spirv, const std::filesystem::path &cdl_output_path);

    CdlContext* GetCDL() const { return cdl_; }
    spv::ExecutionModel GetExecutionModel() const;
    const std::string& GetEntryPoint() const;
    const std::string& GetSourceFile() const;

    // dumps SPRIV to file, returns filename
    std::string DumpShaderCode(const std::string& prefix) const;

   private:
    // dumps SPRIV to file, returns filename
    std::string DumpShaderCode(const std::string& prefix, size_t code_size, const char* code) const;

    CdlContext* cdl_ = nullptr;
    VkShaderModule vk_shader_module_ = VK_NULL_HANDLE;
    spv::ExecutionModel execution_model_ = static_cast<spv::ExecutionModel>(~0);
    std::string entry_point_;
    std::string source_file_;

    std::vector<char> shader_code;

    const std::filesystem::path cdl_output_path_;
};

using ShaderModulePtr = std::unique_ptr<ShaderModule>;

}  // namespace crash_diagnostic_layer
