/*******************************************************************************
 * @file        https://github.com/Zilong-Li/phaseless/src/log.hpp
 * @author      Zilong Li
 * Copyright (C) 2023. The use of this code is governed by the LICENSE file.
 ******************************************************************************/
#ifndef LOG_H_
#define LOG_H_
#include <R_ext/Print.h>

#include <cstring>
#include <fstream>
#include <iomanip>  // setw
#include <iostream>
#include <sstream>

class RConsoleBuffer : public std::streambuf {
 public:
  explicit RConsoleBuffer(bool error) : error_(error) {}

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
      return traits_type::not_eof(ch);
    const char c = traits_type::to_char_type(ch);
    xsputn(&c, 1);
    return ch;
  }

  std::streamsize xsputn(const char* text, std::streamsize size) override {
    if (size <= 0) return 0;
    const std::string value(text, static_cast<size_t>(size));
    if (error_)
      REprintf("%s", value.c_str());
    else
      Rprintf("%s", value.c_str());
    return size;
  }

 private:
  bool error_;
};

inline RConsoleBuffer r_stdout_buffer(false);
inline RConsoleBuffer r_stderr_buffer(true);
inline std::ostream r_cout(&r_stdout_buffer);
inline std::ostream r_cerr(&r_stderr_buffer);

class Logger {
 public:
  std::ofstream cao;
  bool is_screen = false;

  Logger() = default;

  Logger(std::string filename, bool screen = true) {
    is_screen = screen;
    cao.open(filename.c_str());
    if (!cao) throw std::runtime_error(filename + " : " + std::strerror(errno));
    cao.precision(6);
    cao.flags(std::ios::fixed | std::ios::right);
  }

  ~Logger() {}

  template <class S>
  Logger& operator<<(const S& val) {
    cao << val;
    if (is_screen) r_cout << val;
    return *this;
  }

  Logger& operator<<(std::ostream& (*pfun)(std::ostream&)) {
    pfun(cao);
    if (is_screen) pfun(r_cout);
    return *this;
  };

  template <class S>
  void printSpace(std::ostream& os, const S& val) {
    if (std::is_integral_v<std::decay_t<decltype(val)>>)
      os << std::setw(2) << val;
    else if (std::is_floating_point_v<std::decay_t<decltype(val)>>)
      os << val;
    else
      os << val << " ";
  }

  template <typename... Args>
  void print(const Args&... args) {
    (..., printSpace(cao, args));
    cao << std::endl;
    if (is_screen) {
      r_cout.precision(6);
      r_cout.flags(std::ios::fixed | std::ios::right);
      (..., printSpace(r_cout, args));
      r_cout << std::endl;
    }
  }

  // only print to stderr
  template <typename... Args>
  void cerr(const Args&... args) {
    (..., printSpace(r_cerr, args));
    r_cerr << std::endl;
  }

  template <typename... Args>
  void warn(const Args&... args) {
    (..., printSpace(cao, args));
    cao << std::endl;
    if (is_screen) {
      r_cout << "\x1B[33m";
      (..., printSpace(r_cout, args));
      r_cout << "\033[0m" << std::endl;
    }
  }

  template <typename... Args>
  void error(const Args&... args) {
    (..., printSpace(cao, args));
    cao << std::endl;
    std::ostringstream oss;
    oss << "\x1B[31m";
    (..., printSpace(oss, args));
    oss << "\033[0m" << std::endl;
    throw std::runtime_error(oss.str());
  }

  template <typename... Args>
  void done(const Args&... args) {
    (..., printSpace(cao, args));
    cao << std::endl;
    if (is_screen) {
      r_cout << "\x1B[32m";
      (..., printSpace(r_cout, args));
      r_cout << "\033[0m" << std::endl;
    }
  }
};

#endif  // LOG_H_
