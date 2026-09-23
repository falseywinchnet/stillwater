#include "macos_host.hpp"
#include "stillwater/sound.hpp"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
namespace {
bool parse_number(const char* text, double& result) {
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0' || errno != 0 || !std::isfinite(value))
        return false;
    result = value;
    return true;
}
} // namespace
int main(int argc, char** argv) {
    stillwater::Options options{};
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "--preview") == 0)
            options.desktop = false;
        else if (std::strcmp(argument, "--desktop") == 0)
            options.desktop = true;
        else if (std::strcmp(argument, "--muted") == 0)
            options.muted = true;
        else if (std::strcmp(argument, "--paused") == 0)
            options.paused = true;
        else if (std::strcmp(argument, "--smoke-tap") == 0)
            options.smoke_tap = true;
        else if (std::strcmp(argument, "--capture") == 0 && index + 1 < argc)
            options.capture = argv[++index];
        else if (std::strcmp(argument, "--metrics") == 0 && index + 1 < argc)
            options.metrics = argv[++index];
        else if (std::strcmp(argument, "--quit-after") == 0 && index + 1 < argc) {
            if (!parse_number(argv[++index], options.quit_after) || options.quit_after <= 0 ||
                options.quit_after >
                    static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 1e9)
                return 2;
        } else if (std::strcmp(argument, "--fps") == 0 && index + 1 < argc) {
            double fps{};
            if (!parse_number(argv[++index], fps) || fps < 1 || fps > 60 || std::floor(fps) != fps)
                return 2;
            options.fps = static_cast<unsigned int>(fps);
        } else if (std::strcmp(argument, "--msaa") == 0 && index + 1 < argc) {
            double samples{};
            if (!parse_number(argv[++index], samples) || (samples != 2 && samples != 4))
                return 2;
            options.samples = static_cast<unsigned int>(samples);
        } else if (std::strcmp(argument, "--render-scale") == 0 && index + 1 < argc) {
            if (!parse_number(argv[++index], options.render_scale) || options.render_scale < 0.5 ||
                options.render_scale > 1)
                return 2;
        } else if (std::strcmp(argument, "--export-tap") == 0 && index + 1 < argc) {
            const stillwater::Sound sound = stillwater::make_glass_tap(0);
            return stillwater::write_wave(sound, argv[++index]) ? 0 : 1;
        } else if (std::strcmp(argument, "--export-ambience") == 0 && index + 1 < argc) {
            const stillwater::Sound sound = stillwater::make_ambience();
            return stillwater::write_wave(sound, argv[++index]) ? 0 : 1;
        } else {
            std::cout << "Stillwater [--desktop|--preview] [--muted] [--paused] [--fps 1..60]\n  "
                         "[--capture file.png] [--metrics file.json] [--quit-after seconds]\n  "
                         "[--msaa 2|4] [--render-scale 0.5..1]\n  "
                         "[--export-tap file.wav] [--export-ambience file.wav]\n";
            return std::strcmp(argument, "--help") == 0 ? 0 : 2;
        }
    }
    return stillwater::run_macos(options);
}
