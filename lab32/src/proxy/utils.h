#ifndef LAB32_UTILS_H
#define LAB32_UTILS_H

void throw_errno(const std::string& message, bool no_errno = false) {
    throw std::system_error(no_errno ? 0 : errno, std::generic_category(), message);
}

#endif //LAB32_UTILS_H
