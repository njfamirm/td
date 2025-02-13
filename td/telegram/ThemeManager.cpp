//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ThemeManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/BackgroundInfo.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/emoji.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

static bool are_colors_valid(const vector<int32> &colors, size_t min_size, size_t max_size) {
  if (min_size > colors.size() || colors.size() > max_size) {
    return false;
  }
  for (auto &color : colors) {
    if (color < 0 || color > 0xFFFFFF) {
      return false;
    }
  }
  return true;
}

class GetChatThemesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_Themes>> promise_;

 public:
  explicit GetChatThemesQuery(Promise<telegram_api::object_ptr<telegram_api::account_Themes>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getChatThemes(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getChatThemes>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetPeerColorsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::help_PeerColors>> promise_;

 public:
  explicit GetPeerColorsQuery(Promise<telegram_api::object_ptr<telegram_api::help_PeerColors>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 hash) {
    send_query(G()->net_query_creator().create(telegram_api::help_getPeerColors(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getPeerColors>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetPeerProfileColorsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::help_PeerColors>> promise_;

 public:
  explicit GetPeerProfileColorsQuery(Promise<telegram_api::object_ptr<telegram_api::help_PeerColors>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 hash) {
    send_query(G()->net_query_creator().create(telegram_api::help_getPeerProfileColors(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getPeerProfileColors>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

bool operator==(const ThemeManager::ThemeSettings &lhs, const ThemeManager::ThemeSettings &rhs) {
  return lhs.accent_color == rhs.accent_color && lhs.message_accent_color == rhs.message_accent_color &&
         lhs.background_info == rhs.background_info && lhs.base_theme == rhs.base_theme &&
         lhs.message_colors == rhs.message_colors && lhs.animate_message_colors == rhs.animate_message_colors;
}

bool operator!=(const ThemeManager::ThemeSettings &lhs, const ThemeManager::ThemeSettings &rhs) {
  return !(lhs == rhs);
}

bool operator==(const ThemeManager::ProfileAccentColor &lhs, const ThemeManager::ProfileAccentColor &rhs) {
  return lhs.palette_colors_ == rhs.palette_colors_ && lhs.background_colors_ == rhs.background_colors_ &&
         lhs.story_colors_ == rhs.story_colors_;
}

bool operator!=(const ThemeManager::ProfileAccentColor &lhs, const ThemeManager::ProfileAccentColor &rhs) {
  return !(lhs == rhs);
}

template <class StorerT>
void ThemeManager::ThemeSettings::store(StorerT &storer) const {
  using td::store;
  bool has_message_accent_color = message_accent_color != accent_color;
  bool has_background = background_info.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(animate_message_colors);
  STORE_FLAG(has_message_accent_color);
  STORE_FLAG(has_background);
  END_STORE_FLAGS();
  store(accent_color, storer);
  if (has_message_accent_color) {
    store(message_accent_color, storer);
  }
  if (has_background) {
    store(background_info, storer);
  }
  store(base_theme, storer);
  store(message_colors, storer);
}

template <class ParserT>
void ThemeManager::ThemeSettings::parse(ParserT &parser) {
  using td::parse;
  bool has_message_accent_color;
  bool has_background;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(animate_message_colors);
  PARSE_FLAG(has_message_accent_color);
  PARSE_FLAG(has_background);
  END_PARSE_FLAGS();
  parse(accent_color, parser);
  if (has_message_accent_color) {
    parse(message_accent_color, parser);
  } else {
    message_accent_color = accent_color;
  }
  if (has_background) {
    parse(background_info, parser);
  }
  parse(base_theme, parser);
  parse(message_colors, parser);
}

template <class StorerT>
void ThemeManager::ChatTheme::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(emoji, storer);
  td::store(id, storer);
  td::store(light_theme, storer);
  td::store(dark_theme, storer);
}

template <class ParserT>
void ThemeManager::ChatTheme::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(emoji, parser);
  td::parse(id, parser);
  td::parse(light_theme, parser);
  td::parse(dark_theme, parser);
}

template <class StorerT>
void ThemeManager::ChatThemes::store(StorerT &storer) const {
  td::store(hash, storer);
  td::store(themes, storer);
}

template <class ParserT>
void ThemeManager::ChatThemes::parse(ParserT &parser) {
  td::parse(hash, parser);
  td::parse(themes, parser);
}

template <class StorerT>
void ThemeManager::AccentColors::store(StorerT &storer) const {
  bool has_hash = hash_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_hash);
  END_STORE_FLAGS();
  td::store(static_cast<int32>(light_colors_.size()), storer);
  for (auto &it : light_colors_) {
    td::store(it.first, storer);
    td::store(it.second, storer);
  }
  td::store(static_cast<int32>(dark_colors_.size()), storer);
  for (auto &it : dark_colors_) {
    td::store(it.first, storer);
    td::store(it.second, storer);
  }
  td::store(accent_color_ids_, storer);
  if (has_hash) {
    td::store(hash_, storer);
  }
}

template <class ParserT>
void ThemeManager::AccentColors::parse(ParserT &parser) {
  bool has_hash;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_hash);
  END_PARSE_FLAGS();
  int32 size;
  td::parse(size, parser);
  for (int32 i = 0; i < size; i++) {
    AccentColorId accent_color_id;
    vector<int32> colors;
    td::parse(accent_color_id, parser);
    td::parse(colors, parser);
    CHECK(accent_color_id.is_valid());
    light_colors_.emplace(accent_color_id, std::move(colors));
  }
  td::parse(size, parser);
  for (int32 i = 0; i < size; i++) {
    AccentColorId accent_color_id;
    vector<int32> colors;
    td::parse(accent_color_id, parser);
    td::parse(colors, parser);
    CHECK(accent_color_id.is_valid());
    dark_colors_.emplace(accent_color_id, std::move(colors));
  }
  td::parse(accent_color_ids_, parser);
  if (has_hash) {
    td::parse(hash_, parser);
  }
}

template <class StorerT>
void ThemeManager::ProfileAccentColor::store(StorerT &storer) const {
  td::store(palette_colors_, storer);
  td::store(background_colors_, storer);
  td::store(story_colors_, storer);
}

template <class ParserT>
void ThemeManager::ProfileAccentColor::parse(ParserT &parser) {
  td::parse(palette_colors_, parser);
  td::parse(background_colors_, parser);
  td::parse(story_colors_, parser);
}

template <class StorerT>
void ThemeManager::ProfileAccentColors::store(StorerT &storer) const {
  bool has_hash = hash_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_hash);
  END_STORE_FLAGS();
  td::store(static_cast<int32>(light_colors_.size()), storer);
  for (auto &it : light_colors_) {
    td::store(it.first, storer);
    td::store(it.second, storer);
  }
  td::store(static_cast<int32>(dark_colors_.size()), storer);
  for (auto &it : dark_colors_) {
    td::store(it.first, storer);
    td::store(it.second, storer);
  }
  td::store(accent_color_ids_, storer);
  if (has_hash) {
    td::store(hash_, storer);
  }
}

template <class ParserT>
void ThemeManager::ProfileAccentColors::parse(ParserT &parser) {
  bool has_hash;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_hash);
  END_PARSE_FLAGS();
  int32 size;
  td::parse(size, parser);
  for (int32 i = 0; i < size; i++) {
    AccentColorId accent_color_id;
    ProfileAccentColor colors;
    td::parse(accent_color_id, parser);
    td::parse(colors, parser);
    CHECK(accent_color_id.is_valid());
    light_colors_.emplace(accent_color_id, std::move(colors));
  }
  td::parse(size, parser);
  for (int32 i = 0; i < size; i++) {
    AccentColorId accent_color_id;
    ProfileAccentColor colors;
    td::parse(accent_color_id, parser);
    td::parse(colors, parser);
    CHECK(accent_color_id.is_valid());
    dark_colors_.emplace(accent_color_id, std::move(colors));
  }
  td::parse(accent_color_ids_, parser);
  if (has_hash) {
    td::parse(hash_, parser);
  }
}

ThemeManager::ThemeManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  load_accent_colors();
  load_profile_accent_colors();
}

void ThemeManager::start_up() {
  init();
}

void ThemeManager::load_chat_themes() {  // must not be called in constructor, because uses other managers
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  auto log_event_string = G()->td_db()->get_binlog_pmc()->get(get_chat_themes_database_key());
  if (!log_event_string.empty()) {
    auto status = log_event_parse(chat_themes_, log_event_string);
    if (status.is_ok()) {
      send_update_chat_themes();
    } else {
      LOG(ERROR) << "Failed to parse chat themes from binlog: " << status;
      chat_themes_ = ChatThemes();
    }
  }
}

void ThemeManager::load_accent_colors() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  auto log_event_string = G()->td_db()->get_binlog_pmc()->get(get_accent_colors_database_key());
  if (!log_event_string.empty()) {
    auto status = log_event_parse(accent_colors_, log_event_string);
    if (status.is_ok()) {
      send_update_accent_colors();
    } else {
      LOG(ERROR) << "Failed to parse accent colors from binlog: " << status;
      accent_colors_ = AccentColors();
    }
  }
}

void ThemeManager::load_profile_accent_colors() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  auto log_event_string = G()->td_db()->get_binlog_pmc()->get(get_profile_accent_colors_database_key());
  if (!log_event_string.empty()) {
    auto status = log_event_parse(profile_accent_colors_, log_event_string);
    if (status.is_ok()) {
      send_update_profile_accent_colors();
    } else {
      LOG(ERROR) << "Failed to parse profile accent colors from binlog: " << status;
      profile_accent_colors_ = ProfileAccentColors();
    }
  }
}

void ThemeManager::init() {
  load_chat_themes();
  if (td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot()) {
    if (chat_themes_.hash == 0) {
      reload_chat_themes();
    }
    if (accent_colors_.hash_ == 0) {
      reload_accent_colors();
    }
    if (profile_accent_colors_.hash_ == 0) {
      reload_profile_accent_colors();
    }
  }
}

void ThemeManager::tear_down() {
  parent_.reset();
}

bool ThemeManager::is_dark_base_theme(BaseTheme base_theme) {
  switch (base_theme) {
    case BaseTheme::Classic:
    case BaseTheme::Day:
    case BaseTheme::Arctic:
      return false;
    case BaseTheme::Night:
    case BaseTheme::Tinted:
      return true;
    default:
      UNREACHABLE();
      return false;
  }
}

void ThemeManager::on_update_theme(telegram_api::object_ptr<telegram_api::theme> &&theme, Promise<Unit> &&promise) {
  CHECK(theme != nullptr);
  bool is_changed = false;
  bool was_light = false;
  bool was_dark = false;
  for (auto &chat_theme : chat_themes_.themes) {
    if (chat_theme.id == theme->id_) {
      for (auto &settings : theme->settings_) {
        auto theme_settings = get_chat_theme_settings(std::move(settings));
        if (theme_settings.message_colors.empty()) {
          continue;
        }
        if (is_dark_base_theme(theme_settings.base_theme)) {
          if (!was_dark) {
            was_dark = true;
            if (chat_theme.dark_theme != theme_settings) {
              chat_theme.dark_theme = std::move(theme_settings);
              is_changed = true;
            }
          }
        } else {
          if (!was_light) {
            was_light = true;
            if (chat_theme.light_theme != theme_settings) {
              chat_theme.light_theme = std::move(theme_settings);
              is_changed = true;
            }
          }
        }
      }
    }
  }
  if (is_changed) {
    save_chat_themes();
    send_update_chat_themes();
  }
  promise.set_value(Unit());
}

bool ThemeManager::on_update_accent_colors(FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> light_colors,
                                           FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> dark_colors,
                                           vector<AccentColorId> accent_color_ids) {
  auto are_equal = [](const FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> &lhs,
                      const FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> &rhs) {
    for (auto &lhs_it : lhs) {
      auto rhs_it = rhs.find(lhs_it.first);
      if (rhs_it == rhs.end() || rhs_it->second != lhs_it.second) {
        return false;
      }
    }
    return true;
  };
  if (accent_color_ids == accent_colors_.accent_color_ids_ && are_equal(light_colors, accent_colors_.light_colors_) &&
      are_equal(dark_colors, accent_colors_.dark_colors_)) {
    return false;
  }
  for (auto &it : light_colors) {
    accent_colors_.light_colors_[it.first] = std::move(it.second);
  }
  for (auto &it : dark_colors) {
    accent_colors_.dark_colors_[it.first] = std::move(it.second);
  }
  accent_colors_.accent_color_ids_ = std::move(accent_color_ids);

  save_accent_colors();
  send_update_accent_colors();
  return true;
}

bool ThemeManager::on_update_profile_accent_colors(
    FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> light_colors,
    FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> dark_colors,
    vector<AccentColorId> accent_color_ids) {
  auto are_equal = [](const FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> &lhs,
                      const FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> &rhs) {
    for (auto &lhs_it : lhs) {
      auto rhs_it = rhs.find(lhs_it.first);
      if (rhs_it == rhs.end() || rhs_it->second != lhs_it.second) {
        return false;
      }
    }
    return true;
  };
  if (accent_color_ids == profile_accent_colors_.accent_color_ids_ &&
      are_equal(light_colors, profile_accent_colors_.light_colors_) &&
      are_equal(dark_colors, profile_accent_colors_.dark_colors_)) {
    return false;
  }
  for (auto &it : light_colors) {
    profile_accent_colors_.light_colors_[it.first] = std::move(it.second);
  }
  for (auto &it : dark_colors) {
    profile_accent_colors_.dark_colors_[it.first] = std::move(it.second);
  }
  profile_accent_colors_.accent_color_ids_ = std::move(accent_color_ids);

  save_profile_accent_colors();
  send_update_profile_accent_colors();
  return true;
}

namespace {
template <bool for_web_view>
static auto get_color_json(int32 color);

template <>
auto get_color_json<false>(int32 color) {
  return static_cast<int64>(static_cast<uint32>(color) | 0xFF000000);
}

template <>
auto get_color_json<true>(int32 color) {
  string res(7, '#');
  const char *hex = "0123456789abcdef";
  for (int i = 0; i < 3; i++) {
    int32 num = (color >> (i * 8)) & 0xFF;
    res[2 * i + 1] = hex[num >> 4];
    res[2 * i + 2] = hex[num & 15];
  }
  return res;
}

template <bool for_web_view>
string get_theme_parameters_json_string_impl(const td_api::object_ptr<td_api::themeParameters> &theme) {
  if (for_web_view && theme == nullptr) {
    return "null";
  }
  return json_encode<string>(json_object([&theme](auto &o) {
    auto get_color = &get_color_json<for_web_view>;
    o("bg_color", get_color(theme->background_color_));
    o("secondary_bg_color", get_color(theme->secondary_background_color_));
    o("text_color", get_color(theme->text_color_));
    o("hint_color", get_color(theme->hint_color_));
    o("link_color", get_color(theme->link_color_));
    o("button_color", get_color(theme->button_color_));
    o("button_text_color", get_color(theme->button_text_color_));
    o("header_bg_color", get_color(theme->header_background_color_));
    o("section_bg_color", get_color(theme->section_background_color_));
    o("accent_text_color", get_color(theme->accent_text_color_));
    o("section_header_text_color", get_color(theme->section_header_text_color_));
    o("subtitle_text_color", get_color(theme->subtitle_text_color_));
    o("destructive_text_color", get_color(theme->destructive_text_color_));
  }));
}
}  // namespace

string ThemeManager::get_theme_parameters_json_string(const td_api::object_ptr<td_api::themeParameters> &theme,
                                                      bool for_web_view) {
  if (for_web_view) {
    return get_theme_parameters_json_string_impl<true>(theme);
  } else {
    return get_theme_parameters_json_string_impl<false>(theme);
  }
}

int32 ThemeManager::get_accent_color_id_object(AccentColorId accent_color_id,
                                               AccentColorId fallback_accent_color_id) const {
  CHECK(accent_color_id.is_valid());
  if (td_->auth_manager_->is_bot() || accent_color_id.is_built_in() ||
      accent_colors_.light_colors_.count(accent_color_id) != 0) {
    return accent_color_id.get();
  }
  if (!fallback_accent_color_id.is_valid()) {
    return 5;  // blue
  }
  CHECK(fallback_accent_color_id.is_built_in());
  return fallback_accent_color_id.get();
}

int32 ThemeManager::get_profile_accent_color_id_object(AccentColorId accent_color_id) const {
  if (!accent_color_id.is_valid()) {
    return -1;
  }
  if (td_->auth_manager_->is_bot() || profile_accent_colors_.light_colors_.count(accent_color_id) != 0) {
    return accent_color_id.get();
  }
  return -1;
}

td_api::object_ptr<td_api::themeSettings> ThemeManager::get_theme_settings_object(const ThemeSettings &settings) const {
  auto fill = [colors = settings.message_colors]() mutable -> td_api::object_ptr<td_api::BackgroundFill> {
    if (colors.size() >= 3) {
      return td_api::make_object<td_api::backgroundFillFreeformGradient>(std::move(colors));
    }
    CHECK(!colors.empty());
    if (colors.size() == 1 || colors[0] == colors[1]) {
      return td_api::make_object<td_api::backgroundFillSolid>(colors[0]);
    }
    return td_api::make_object<td_api::backgroundFillGradient>(colors[1], colors[0], 0);
  }();

  // ignore settings.base_theme for now
  return td_api::make_object<td_api::themeSettings>(
      settings.accent_color, settings.background_info.get_background_object(td_), std::move(fill),
      settings.animate_message_colors, settings.message_accent_color);
}

td_api::object_ptr<td_api::chatTheme> ThemeManager::get_chat_theme_object(const ChatTheme &theme) const {
  return td_api::make_object<td_api::chatTheme>(theme.emoji, get_theme_settings_object(theme.light_theme),
                                                get_theme_settings_object(theme.dark_theme));
}

td_api::object_ptr<td_api::updateChatThemes> ThemeManager::get_update_chat_themes_object() const {
  return td_api::make_object<td_api::updateChatThemes>(
      transform(chat_themes_.themes, [this](const ChatTheme &theme) { return get_chat_theme_object(theme); }));
}

td_api::object_ptr<td_api::updateAccentColors> ThemeManager::get_update_accent_colors_object() const {
  return accent_colors_.get_update_accent_colors_object();
}

td_api::object_ptr<td_api::updateAccentColors> ThemeManager::AccentColors::get_update_accent_colors_object() const {
  vector<td_api::object_ptr<td_api::accentColor>> colors;
  int32 base_colors[] = {0xDF2020, 0xDFA520, 0xA040A0, 0x208020, 0x20DFDF, 0x2044DF, 0xDF1493};
  auto get_distance = [](int32 lhs_color, int32 rhs_color) {
    auto get_color_distance = [](int32 lhs, int32 rhs) {
      auto diff = max(lhs & 255, 0) - max(rhs & 255, 0);
      return diff * diff;
    };
    return get_color_distance(lhs_color, rhs_color) + get_color_distance(lhs_color >> 8, rhs_color >> 8) +
           get_color_distance(lhs_color >> 16, rhs_color >> 16);
  };
  for (auto &it : light_colors_) {
    auto light_colors = it.second;
    auto dark_it = dark_colors_.find(it.first);
    auto dark_colors = dark_it != dark_colors_.end() ? dark_it->second : light_colors;
    CHECK(!light_colors.empty());
    CHECK(!dark_colors.empty());
    auto first_color = light_colors[0];
    int best_index = 0;
    int32 best_distance = get_distance(base_colors[0], first_color);
    for (int i = 1; i < 7; i++) {
      auto cur_distance = get_distance(base_colors[i], first_color);
      if (cur_distance < best_distance) {
        best_distance = cur_distance;
        best_index = i;
      }
    }
    colors.push_back(td_api::make_object<td_api::accentColor>(it.first.get(), best_index, std::move(light_colors),
                                                              std::move(dark_colors)));
  }
  auto available_accent_color_ids =
      transform(accent_color_ids_, [](AccentColorId accent_color_id) { return accent_color_id.get(); });
  return td_api::make_object<td_api::updateAccentColors>(std::move(colors), std::move(available_accent_color_ids));
}

td_api::object_ptr<td_api::updateProfileAccentColors> ThemeManager::get_update_profile_accent_colors_object() const {
  return profile_accent_colors_.get_update_profile_accent_colors_object();
}

bool ThemeManager::ProfileAccentColor::is_valid() const {
  return are_colors_valid(palette_colors_, 1, 2) && are_colors_valid(background_colors_, 1, 2) &&
         are_colors_valid(story_colors_, 2, 2);
}

td_api::object_ptr<td_api::profileAccentColors> ThemeManager::ProfileAccentColor::get_profile_accent_colors_object()
    const {
  return td_api::make_object<td_api::profileAccentColors>(
      vector<int32>(palette_colors_), vector<int32>(background_colors_), vector<int32>(story_colors_));
}

td_api::object_ptr<td_api::updateProfileAccentColors>
ThemeManager::ProfileAccentColors::get_update_profile_accent_colors_object() const {
  vector<td_api::object_ptr<td_api::profileAccentColor>> colors;
  for (auto &it : light_colors_) {
    auto light_colors = it.second.get_profile_accent_colors_object();
    auto dark_it = dark_colors_.find(it.first);
    auto dark_colors = dark_it != dark_colors_.end() ? dark_it->second.get_profile_accent_colors_object()
                                                     : it.second.get_profile_accent_colors_object();
    colors.push_back(td_api::make_object<td_api::profileAccentColor>(it.first.get(), std::move(light_colors),
                                                                     std::move(dark_colors)));
  }
  auto available_accent_color_ids =
      transform(accent_color_ids_, [](AccentColorId accent_color_id) { return accent_color_id.get(); });
  return td_api::make_object<td_api::updateProfileAccentColors>(std::move(colors),
                                                                std::move(available_accent_color_ids));
}

string ThemeManager::get_chat_themes_database_key() {
  return "chat_themes";
}

string ThemeManager::get_accent_colors_database_key() {
  return "accent_colors";
}

string ThemeManager::get_profile_accent_colors_database_key() {
  return "profile_accent_colors";
}

void ThemeManager::save_chat_themes() {
  G()->td_db()->get_binlog_pmc()->set(get_chat_themes_database_key(), log_event_store(chat_themes_).as_slice().str());
}

void ThemeManager::save_accent_colors() {
  G()->td_db()->get_binlog_pmc()->set(get_accent_colors_database_key(),
                                      log_event_store(accent_colors_).as_slice().str());
}

void ThemeManager::save_profile_accent_colors() {
  G()->td_db()->get_binlog_pmc()->set(get_profile_accent_colors_database_key(),
                                      log_event_store(profile_accent_colors_).as_slice().str());
}

void ThemeManager::send_update_chat_themes() const {
  send_closure(G()->td(), &Td::send_update, get_update_chat_themes_object());
}

void ThemeManager::send_update_accent_colors() const {
  send_closure(G()->td(), &Td::send_update, get_update_accent_colors_object());
}

void ThemeManager::send_update_profile_accent_colors() const {
  send_closure(G()->td(), &Td::send_update, get_update_profile_accent_colors_object());
}

void ThemeManager::reload_chat_themes() {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_Themes>> result) {
        send_closure(actor_id, &ThemeManager::on_get_chat_themes, std::move(result));
      });

  td_->create_handler<GetChatThemesQuery>(std::move(request_promise))->send(chat_themes_.hash);
}

void ThemeManager::on_get_chat_themes(Result<telegram_api::object_ptr<telegram_api::account_Themes>> result) {
  if (result.is_error()) {
    return;
  }

  auto chat_themes_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(chat_themes_ptr);
  if (chat_themes_ptr->get_id() == telegram_api::account_themesNotModified::ID) {
    return;
  }
  CHECK(chat_themes_ptr->get_id() == telegram_api::account_themes::ID);
  auto chat_themes = telegram_api::move_object_as<telegram_api::account_themes>(chat_themes_ptr);
  chat_themes_.hash = chat_themes->hash_;
  chat_themes_.themes.clear();
  for (auto &theme : chat_themes->themes_) {
    if (!is_emoji(theme->emoticon_) || !theme->for_chat_) {
      LOG(ERROR) << "Receive " << to_string(theme);
      continue;
    }

    bool was_light = false;
    bool was_dark = false;
    ChatTheme chat_theme;
    chat_theme.emoji = std::move(theme->emoticon_);
    chat_theme.id = theme->id_;
    for (auto &settings : theme->settings_) {
      auto theme_settings = get_chat_theme_settings(std::move(settings));
      if (theme_settings.message_colors.empty()) {
        continue;
      }
      if (is_dark_base_theme(theme_settings.base_theme)) {
        if (!was_dark) {
          was_dark = true;
          if (chat_theme.dark_theme != theme_settings) {
            chat_theme.dark_theme = std::move(theme_settings);
          }
        }
      } else {
        if (!was_light) {
          was_light = true;
          if (chat_theme.light_theme != theme_settings) {
            chat_theme.light_theme = std::move(theme_settings);
          }
        }
      }
    }
    if (chat_theme.light_theme.message_colors.empty() || chat_theme.dark_theme.message_colors.empty()) {
      continue;
    }
    chat_themes_.themes.push_back(std::move(chat_theme));
  }

  save_chat_themes();
  send_update_chat_themes();
}

void ThemeManager::reload_accent_colors() {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::help_PeerColors>> result) {
        send_closure(actor_id, &ThemeManager::on_get_accent_colors, std::move(result));
      });

  td_->create_handler<GetPeerColorsQuery>(std::move(request_promise))->send(accent_colors_.hash_);
}

void ThemeManager::on_get_accent_colors(Result<telegram_api::object_ptr<telegram_api::help_PeerColors>> result) {
  if (result.is_error()) {
    return;
  }

  auto peer_colors_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(peer_colors_ptr);
  if (peer_colors_ptr->get_id() == telegram_api::help_peerColorsNotModified::ID) {
    return;
  }
  CHECK(peer_colors_ptr->get_id() == telegram_api::help_peerColors::ID);
  auto peer_colors = telegram_api::move_object_as<telegram_api::help_peerColors>(peer_colors_ptr);
  FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> light_colors;
  FlatHashMap<AccentColorId, vector<int32>, AccentColorIdHash> dark_colors;
  vector<AccentColorId> accent_color_ids;
  for (auto &option : peer_colors->colors_) {
    if ((option->colors_ != nullptr && option->colors_->get_id() != telegram_api::help_peerColorSet::ID) ||
        (option->dark_colors_ != nullptr && option->dark_colors_->get_id() != telegram_api::help_peerColorSet::ID)) {
      LOG(ERROR) << "Receive " << to_string(option);
      continue;
    }
    AccentColorId accent_color_id(option->color_id_);
    if (!accent_color_id.is_valid() || td::contains(accent_color_ids, accent_color_id) ||
        (accent_color_id.is_built_in() && (option->colors_ != nullptr || option->dark_colors_ != nullptr)) ||
        (!accent_color_id.is_built_in() && option->colors_ == nullptr)) {
      LOG(ERROR) << "Receive " << to_string(option);
      continue;
    }
    bool is_valid = true;
    vector<int32> current_light_colors;
    vector<int32> current_dark_colors;
    if (option->colors_ != nullptr) {
      auto colors = telegram_api::move_object_as<telegram_api::help_peerColorSet>(option->colors_);
      current_light_colors = std::move(colors->colors_);
      if (!are_colors_valid(current_light_colors, 1, 3)) {
        is_valid = false;
      }
    }
    if (option->dark_colors_ != nullptr) {
      auto colors = telegram_api::move_object_as<telegram_api::help_peerColorSet>(option->dark_colors_);
      current_dark_colors = std::move(colors->colors_);
      if (!are_colors_valid(current_dark_colors, 1, 3)) {
        is_valid = false;
      }
    }
    if (!is_valid) {
      LOG(ERROR) << "Receive invalid colors for " << accent_color_id;
      continue;
    }
    if (!option->hidden_) {
      accent_color_ids.push_back(accent_color_id);
    }
    if (!current_light_colors.empty()) {
      light_colors[accent_color_id] = std::move(current_light_colors);
    }
    if (!current_dark_colors.empty()) {
      dark_colors[accent_color_id] = std::move(current_dark_colors);
    }
  }

  bool is_changed = false;
  if (accent_colors_.hash_ != peer_colors->hash_) {
    accent_colors_.hash_ = peer_colors->hash_;
    is_changed = true;
  }
  if (!on_update_accent_colors(std::move(light_colors), std::move(dark_colors), std::move(accent_color_ids)) &&
      is_changed) {
    save_accent_colors();
  }
}

void ThemeManager::reload_profile_accent_colors() {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::help_PeerColors>> result) {
        send_closure(actor_id, &ThemeManager::on_get_profile_accent_colors, std::move(result));
      });

  td_->create_handler<GetPeerProfileColorsQuery>(std::move(request_promise))->send(profile_accent_colors_.hash_);
}

ThemeManager::ProfileAccentColor ThemeManager::get_profile_accent_color(
    telegram_api::object_ptr<telegram_api::help_PeerColorSet> &&color_set) const {
  CHECK(color_set != nullptr);
  CHECK(color_set->get_id() == telegram_api::help_peerColorProfileSet::ID);
  auto colors = telegram_api::move_object_as<telegram_api::help_peerColorProfileSet>(color_set);
  ProfileAccentColor color;
  color.palette_colors_ = std::move(colors->palette_colors_);
  color.background_colors_ = std::move(colors->bg_colors_);
  color.story_colors_ = std::move(colors->story_colors_);
  return color;
}

void ThemeManager::on_get_profile_accent_colors(
    Result<telegram_api::object_ptr<telegram_api::help_PeerColors>> result) {
  if (result.is_error()) {
    return;
  }

  auto peer_colors_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(peer_colors_ptr);
  if (peer_colors_ptr->get_id() == telegram_api::help_peerColorsNotModified::ID) {
    return;
  }
  CHECK(peer_colors_ptr->get_id() == telegram_api::help_peerColors::ID);
  auto peer_colors = telegram_api::move_object_as<telegram_api::help_peerColors>(peer_colors_ptr);
  FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> light_colors;
  FlatHashMap<AccentColorId, ProfileAccentColor, AccentColorIdHash> dark_colors;
  vector<AccentColorId> accent_color_ids;
  for (auto &option : peer_colors->colors_) {
    AccentColorId accent_color_id(option->color_id_);
    if (option->colors_ == nullptr || option->colors_->get_id() != telegram_api::help_peerColorProfileSet::ID ||
        option->dark_colors_ == nullptr ||
        option->dark_colors_->get_id() != telegram_api::help_peerColorProfileSet::ID || !accent_color_id.is_valid() ||
        td::contains(accent_color_ids, accent_color_id)) {
      LOG(ERROR) << "Receive " << to_string(option);
      continue;
    }
    auto current_light_color = get_profile_accent_color(std::move(option->colors_));
    auto current_dark_color = get_profile_accent_color(std::move(option->dark_colors_));
    if (!current_light_color.is_valid() || !current_dark_color.is_valid()) {
      LOG(ERROR) << "Receive invalid colors for " << accent_color_id;
      continue;
    }
    if (!option->hidden_) {
      accent_color_ids.push_back(accent_color_id);
    }
    light_colors[accent_color_id] = std::move(current_light_color);
    dark_colors[accent_color_id] = std::move(current_dark_color);
  }

  bool is_changed = false;
  if (profile_accent_colors_.hash_ != peer_colors->hash_) {
    profile_accent_colors_.hash_ = peer_colors->hash_;
    is_changed = true;
  }
  if (!on_update_profile_accent_colors(std::move(light_colors), std::move(dark_colors), std::move(accent_color_ids)) &&
      is_changed) {
    save_profile_accent_colors();
  }
}

ThemeManager::BaseTheme ThemeManager::get_base_theme(
    const telegram_api::object_ptr<telegram_api::BaseTheme> &base_theme) {
  CHECK(base_theme != nullptr);
  switch (base_theme->get_id()) {
    case telegram_api::baseThemeClassic::ID:
      return BaseTheme::Classic;
    case telegram_api::baseThemeDay::ID:
      return BaseTheme::Day;
    case telegram_api::baseThemeNight::ID:
      return BaseTheme::Night;
    case telegram_api::baseThemeTinted::ID:
      return BaseTheme::Tinted;
    case telegram_api::baseThemeArctic::ID:
      return BaseTheme::Arctic;
    default:
      UNREACHABLE();
      return BaseTheme::Classic;
  }
}

ThemeManager::ThemeSettings ThemeManager::get_chat_theme_settings(
    telegram_api::object_ptr<telegram_api::themeSettings> settings) {
  ThemeSettings result;
  if (settings != nullptr && !settings->message_colors_.empty() && settings->message_colors_.size() <= 4) {
    result.accent_color = settings->accent_color_;
    bool has_outbox_accent_color = (settings->flags_ & telegram_api::themeSettings::OUTBOX_ACCENT_COLOR_MASK) != 0;
    result.message_accent_color = (has_outbox_accent_color ? settings->outbox_accent_color_ : result.accent_color);
    result.background_info = BackgroundInfo(td_, std::move(settings->wallpaper_));
    result.base_theme = get_base_theme(settings->base_theme_);
    result.message_colors = std::move(settings->message_colors_);
    result.animate_message_colors = settings->message_colors_animated_;
  }
  return result;
}

void ThemeManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  if (!chat_themes_.themes.empty()) {
    updates.push_back(get_update_chat_themes_object());
  }
  if (!accent_colors_.accent_color_ids_.empty()) {
    updates.push_back(get_update_accent_colors_object());
  }
  if (!profile_accent_colors_.accent_color_ids_.empty()) {
    updates.push_back(get_update_profile_accent_colors_object());
  }
}

}  // namespace td
