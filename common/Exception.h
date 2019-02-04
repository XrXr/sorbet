#ifndef SORBET_ERRO_H
#define SORBET_ERRO_H

#include "absl/strings/str_cat.h"
#include "common/os/os.h"
#include "spdlog/spdlog.h"
#include <cstdio>
#include <memory>
#include <string>

namespace sorbet {
extern std::shared_ptr<spdlog::logger> fatalLogger;
class SorbetException : std::logic_error {
public:
    SorbetException(const std::string &message) : logic_error(message) {}
    SorbetException(const char *message) : logic_error(message) {}
};

class FileNotFoundException : SorbetException {
public:
    FileNotFoundException() : SorbetException("File not found") {}
};

class Exception final {
public:
    template <typename... TArgs>[[noreturn]] static bool raise(const TArgs &... args) __attribute__((noreturn));

    [[noreturn]] static inline void notImplemented() {
        raise("Not Implemented");
    }

    static void print_backtrace() noexcept;
    static void failInFuzzer() noexcept;

    template <typename... TArgs>
    [[noreturn]] static inline bool enforce_handler(std::string check, std::string file, int line,
                                                    const TArgs &... args) __attribute__((noreturn)) {
        raise(file + ":" + std::to_string(line), " enforced condition ", check, " has failed: ", args...);
    }
};

template <typename... TArgs>[[noreturn]] bool Exception::raise(const TArgs &... args) {
    Exception::failInFuzzer();
    std::string message = absl::StrCat("", args...);

    if (message.size() > 0) {
        fatalLogger->error("Exception::raise(): {}\n", message);
    } else {
        fatalLogger->error("Exception::raise() (sadly without a message)\n");
    }
    print_backtrace();
    stopInDebugger();
    throw SorbetException(message);
}
} // namespace sorbet
#endif // SORBET_ERRO_H