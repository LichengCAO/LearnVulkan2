#include "utils.h"
#include <filesystem>
#include <fstream>
#include <utility>

void common_utils::ReadFile(const std::string& _filePath, std::vector<uint8_t>& _output)
{
	std::ifstream file(_filePath, std::ios::ate | std::ios::binary);

	CHECK_TRUE(file.is_open(), "Failed to open file!");

	const std::streamoff fileSize = file.tellg();
	CHECK_TRUE(std::in_range<size_t>(fileSize) && std::in_range<std::streamsize>(fileSize),
		"Invalid file size: " + _filePath);
	std::vector<uint8_t> contents(static_cast<size_t>(fileSize));
	if (!contents.empty())
	{
		file.seekg(0);
		CHECK_TRUE(file.read(reinterpret_cast<char*>(contents.data()), static_cast<std::streamsize>(fileSize)),
			"Failed to read file: " + _filePath);
	}
	_output.swap(contents);
}

auto common_utils::GetFileExtension(const std::string& _filePath) -> std::string
{
	const auto extension = std::filesystem::path(_filePath).extension().string();
	return extension.empty() ? extension : extension.substr(1);
}
