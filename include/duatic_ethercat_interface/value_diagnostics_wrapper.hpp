/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <optional>
#include <algorithm>
#include <utility>

namespace duatic::ethercat_interface
{
template <typename T, typename DiagnosticsT>
class [[nodiscard]] ValueDiagnosticsWrapper
{
public:
  using value_type = T;
  using diagnostics_type = DiagnosticsT;

  ValueDiagnosticsWrapper() = default;

  explicit ValueDiagnosticsWrapper(DiagnosticsT diagnostics) : m_diagnostics{ std::move(diagnostics) }
  {
  }

  ValueDiagnosticsWrapper(DiagnosticsT diagnostics, T value) : m_diagnostics{ std::move(diagnostics) }, m_value{ value }
  {
  }

  // --- optional-like interface ---

  [[nodiscard]] bool has_value() const noexcept
  {
    return m_value.has_value();
  }

  /// Disabled for T = bool: `if (v)` as would be ambiguous
  [[nodiscard]] explicit operator bool() const noexcept requires(!std::is_same_v<std::remove_cv_t<T>, bool>)
  {
    return m_value.has_value();
  }

  const T& operator*() const noexcept
  {
    return *m_value;
  }
  T& operator*() noexcept
  {
    return *m_value;
  }

  const T* operator->() const noexcept
  {
    return m_value.operator->();
  }
  T* operator->() noexcept
  {
    return m_value.operator->();
  }

  const T& value() const
  {
    return m_value.value();
  }
  T& value()
  {
    return m_value.value();
  }

  [[nodiscard]] T value_or(T fallback) const noexcept
  {
    return m_value.value_or(fallback);
  }

  // --- diagnostics (DiagnosticsT is *not* assumed trivial) ---

  [[nodiscard]] const DiagnosticsT& diagnostics() const& noexcept
  {
    return m_diagnostics;
  }
  [[nodiscard]] DiagnosticsT diagnostics() && noexcept
  {
    return std::move(m_diagnostics);
  }

  // --- interop with plain std::optional ---

  /// Implicit so existing `std::optional<T> x = read(...);` call sites keep
  /// compiling unchanged. Deliberately discards the diagnostics.
  [[nodiscard]] operator std::optional<T>() const noexcept
  {
    return m_value;
  }

  [[nodiscard]] const std::optional<T>& as_optional() const noexcept
  {
    return m_value;
  }

  // --- mapping (diagnostics are preserved across the transform) ---

  template <typename F>
  [[nodiscard]] auto transform(F&& func) const
  {
    using U = std::remove_cvref_t<std::invoke_result_t<F, T>>;
    if (!m_value.has_value()) {
      return ValueDiagnosticsWrapper<U, DiagnosticsT>{ m_diagnostics };
    }
    return ValueDiagnosticsWrapper<U, DiagnosticsT>{ m_diagnostics, std::invoke(std::forward<F>(func), *m_value) };
  }

private:
  DiagnosticsT m_diagnostics{};
  std::optional<T> m_value{};
};

}  // namespace duatic::ethercat_interface
