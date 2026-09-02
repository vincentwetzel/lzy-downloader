#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool hasArgument(const std::vector<std::string> &arguments, const std::string &value)
{
    for (const std::string &argument : arguments) {
        if (argument == value) {
            return true;
        }
    }
    return false;
}

std::string findUrl(const std::vector<std::string> &arguments)
{
    for (const std::string &argument : arguments) {
        if (argument.rfind("http://", 0) == 0 || argument.rfind("https://", 0) == 0) {
            return argument;
        }
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const std::string url = findUrl(arguments);
    const bool isProbe = hasArgument(arguments, "--dump-single-json")
                         && hasArgument(arguments, "--no-download");
    if (isProbe && (url.find("slow-probe") != std::string::npos
                    || url.find("playlist") != std::string::npos)) {
        // The production watchdog is 45 seconds. Keep this process alive long
        // enough that a successful probe can never mask a missing watchdog.
        std::this_thread::sleep_for(std::chrono::seconds(120));
        return 0;
    }

    if (isProbe) {
        std::cout << R"({"id":"fake-probe","title":"Fake probe"})" << std::endl;
        return 0;
    }

    std::string outputTemplate;
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == "-o") {
            outputTemplate = arguments[index + 1];
            break;
        }
    }
    if (outputTemplate.empty()) {
        std::cerr << "fake yt-dlp did not receive an output template" << std::endl;
        return 2;
    }

    if (url.find("thread-regression") != std::string::npos) {
        // Keep the worker alive long enough for the manager test to inspect
        // its dedicated thread while output processing is active.
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    const std::string id = "ordinary-fallback";
    const std::string extension = "webm";
    std::string outputPath = outputTemplate;
    const auto replaceAll = [&outputPath](const std::string &needle, const std::string &replacement) {
        std::size_t position = 0;
        while ((position = outputPath.find(needle, position)) != std::string::npos) {
            outputPath.replace(position, needle.size(), replacement);
            position += replacement.size();
        }
    };
    replaceAll("%(id)s", id);
    replaceAll("%(ext)s", extension);

    const std::filesystem::path outputFile(outputPath);
    std::error_code error;
    std::filesystem::create_directories(outputFile.parent_path(), error);
    if (error) {
        std::cerr << "failed to create fake output directory: " << error.message() << std::endl;
        return 3;
    }

    std::ofstream media(outputFile, std::ios::binary);
    media << "fake media produced after playlist probe fallback\n";
    media.close();
    if (!media) {
        std::cerr << "failed to write fake media output" << std::endl;
        return 4;
    }

    std::cout << "LZY_FINAL_PATH:" << outputFile.string() << std::endl;
    return 0;
}
