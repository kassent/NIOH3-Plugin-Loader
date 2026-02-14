#pragma once
#include <string>
#include <string_view>
#include <filesystem>
#include <cstdint>
#include <span>
namespace FileUtils {
    /**
     * @brief Get the Documents Directory object
     * 
     * @return std::string_view
     */
    std::string_view GetDocumentsDirectory();
    /**
     * @brief Get the Executable Path object
     * 
     * @return std::filesystem::path
     */
    std::filesystem::path GetExecutablePath();
    /**
     * 读取二进制文件的所有内容到一个字符串中
     * 
     * @param filepath 要读取的文件路径
     * @return 包含文件二进制内容的字符串，如果读取失败则返回空字符串
     */
    std::string ReadBinaryFile(const std::filesystem::path& filepath);
    
    /**
     * 将二进制内容写入文件
     * 
     * @param filepath 目标文件路径
     * @param data 要写入的二进制数据
     * @return 写入成功返回true，失败返回false
     * @note 如果文件路径的目录不存在，会自动创建；如果文件已存在，会覆盖它
     */
    bool WriteBinaryFile(const std::filesystem::path& filepath, std::string_view data);
    bool WriteBinaryFile(const std::filesystem::path& filepath, std::span<const uint8_t> data);


    /**
     * @brief Get the Current Module Path
     * 
     * @return std::filesystem::path
     */
    std::filesystem::path GetCurrentModulePath();
    /**
     * @brief Get the Current Module Name without extension
     * 
     * @return std::string
     */
    std::string GetCurrentModuleName();
}

