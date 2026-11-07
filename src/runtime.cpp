#include "game_ui.hpp"
#include "runtime.hpp"
#include "assets.hpp"
#include "graphics.hpp"
#include "content.hpp"
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bb {
namespace {
Sound loadPackagedSound(const std::string& name) {
    auto bytes = readAsset(name);
    auto extension = std::filesystem::path(name).extension().string();
    Wave wave = LoadWaveFromMemory(extension.c_str(), bytes.data(), static_cast<int>(bytes.size()));
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}
constexpr double radians = 3.14159265358979323846 / 180.0;
std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool numeric(const std::string& text, double& result) {
    if (text.empty() || text.find_first_not_of(" \t\r\n") == std::string::npos) { result = 0; return true; }
    char* end = nullptr;
    result = std::strtod(text.c_str(), &end);
    while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    return end != text.c_str() && end && *end == '\0' && !std::isnan(result);
}
std::string field(const Block& block, const std::string& name) {
    auto it = block.fields.find(name);
    return it == block.fields.end() ? "" : it->second;
}
struct Reader {
    std::istringstream stream;
    explicit Reader(const std::filesystem::path& file) {
        auto bytes = readAsset(file.generic_string());
        stream.str(std::string(bytes.begin(), bytes.end()));
    }
    template<class T> T raw() {
        T value{};
        if (!stream.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("Truncated project image");
        return value;
    }
    int integer() { return raw<std::int32_t>(); }
    int count() { int n = integer(); if (n < 0 || n > 1'000'000) throw std::runtime_error("Invalid image count"); return n; }
    double number() { return raw<double>(); }
    std::string string() { std::string s(count(), '\0'); if (!stream.read(s.data(), static_cast<std::streamsize>(s.size()))) throw std::runtime_error("Truncated image string"); return s; }
    Fields fields() { Fields result; int n = count(); while (n--) { auto key = string(); auto value = string(); result.emplace(std::move(key), std::move(value)); } return result; }
};
}
Value::Value(double n) {
    if (std::isnan(n)) { text = "NaN"; return; }
    if (std::isinf(n)) { text = n < 0 ? "-Infinity" : "Infinity"; return; }
    if (n == 0) { text = "0"; return; }
    char buffer[64];
    auto converted = std::to_chars(buffer, buffer + sizeof(buffer), n, std::chars_format::general);
    text.assign(buffer, converted.ptr);
}
double Value::number() const { double n{}; return numeric(text, n) ? n : 0; }
bool Value::truth() const { return !text.empty() && text != "0" && lower(text) != "false"; }
bool equal(const Value& a, const Value& b) {
    double x{}, y{};
    // Scratch compares whitespace-only strings lexically rather than as zero.
    if (!a.text.empty() && !b.text.empty() && a.text.find_first_not_of(" \t\r\n") != std::string::npos && b.text.find_first_not_of(" \t\r\n") != std::string::npos && numeric(a.text, x) && numeric(b.text, y)) return x == y;
    return lower(a.text) == lower(b.text);
}
Runtime::Runtime(std::filesystem::path path, bool noWindow) : root(std::move(path)), headless(noWindow) {}
Runtime::~Runtime() {
    for (auto& [name, item] : images) {
        if (item.texture.id) gfx::release(item.texture);
        if (item.image.data) UnloadImage(item.image);
    }
    for (auto& [name, item] : sounds) UnloadSound(item);
}
void Runtime::load() {
    Reader r("scratch/project.bbdx");
    const std::string magic("BBDX\1\0\0\0", 8);
    for (unsigned char byte : magic) if (r.raw<unsigned char>() != byte) throw std::runtime_error("Invalid project image version");
    std::vector<std::string> ops;
    int n = r.count(); while (n--) ops.push_back(r.string());
    n = r.count();
    while (n--) {
        Definition d;
        d.name = r.string(); d.x = r.number(); d.y = r.number(); d.size = r.number(); d.direction = r.number(); d.volume = r.number();
        d.costume = r.integer(); d.visible = r.integer(); d.layer = r.integer(); d.rotation = r.string();
        int count = r.count();
        while (count--) { auto id = r.string(); auto name = r.string(); auto value = r.string(); d.variables[id] = value; d.variableNames[id] = name; }
        count = r.count();
        while (count--) { auto id = r.string(); d.listNames[id] = r.string(); int items = r.count(); while (items--) d.lists[id].emplace_back(r.string()); }
        for (auto* assets : {&d.costumes, &d.sounds}) {
            count = r.count();
            while (count--) { Asset a; a.name = r.string(); a.file = r.string(); a.cx = r.number(); a.cy = r.number(); a.resolution = r.number(); assets->push_back(std::move(a)); }
        }
        count = r.count();
        while (count--) {
            Block b;
            b.id = r.string(); int op = r.integer(); if (op < 0 || op >= static_cast<int>(ops.size())) throw std::runtime_error("Invalid opcode");
            b.op = ops[op]; b.next = r.integer(); b.top = r.integer(); b.fields = r.fields(); b.procedure = r.string(); b.warp = r.integer();
            int args = r.count(); while (args--) { auto id = r.string(); auto name = r.string(); b.arguments.emplace_back(id, name); }
            int inputs = r.count(); while (inputs--) { auto name = r.string(); Input in; in.kind = r.integer(); in.block = r.integer(); in.value = r.string(); b.inputs[name] = std::move(in); }
            d.blocks.push_back(std::move(b));
        }
        for (auto& block : d.blocks) if (block.op == "procedures_definition") {
            auto prototype = block.inputs.find("custom_block");
            if (prototype != block.inputs.end() && prototype->second.block >= 0) d.procedures[d.blocks.at(prototype->second.block).procedure] = static_cast<int>(&block - d.blocks.data());
        }
        definitions.push_back(std::move(d));
    }
    if (definitions.empty()) throw std::runtime_error("No targets in image");

}
void Runtime::setDeterministic(std::uint32_t seed, std::int64_t unixSeconds) {
    if (unixSeconds < 946684800 || unixSeconds > 4102444800LL) {
        throw std::runtime_error("Replay epoch must be between 2000 and 2100");
    }
    deterministic = true;
    randomSeed = seed;
    epochSeconds = unixSeconds;
}
void Runtime::applyInput(const InputFrame& frame) {
    inputSystem.submit(decodeInput(frame));
    inputOrigin = InputOrigin::Replay;
}
void Runtime::submitLiveInput(const RawInput& frame) {
    inputSystem.submit(frame);
    inputOrigin = InputOrigin::Live;
}
void Runtime::updatePlayerInput() {
    std::vector<std::string> controls;
    if (!actors.empty()) {
        for (const auto& [id, name] : definitions[0].listNames) {
            if (name == ":CONTROLS:") {
                for (const auto& value : actors[0]->lists[id]) controls.push_back(value.text);
                break;
            }
        }
    }
    mappedInput = inputSystem.mapPlayer(ControlBindings::fromLegacy(controls),
                                       cameraSystem.stageToWorld(inputSystem.raw().pointer));
}
double Runtime::randomUnit() {
    const auto high = random() >> 5;
    const auto low = random() >> 6;
    randomDraws += 2;
    return (high * 67108864.0 + low) / 9007199254740992.0;
}
double Runtime::calendarSeconds() const {
    if (deterministic) return static_cast<double>(epochSeconds) + now;
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
int Runtime::matchSecondsRemaining() const {
    const auto minutes = static_cast<int>(namedValue("&Spill-lengde").number());
    if (minutes <= 0) return 0;
    const auto seconds = static_cast<std::time_t>(calendarSeconds());
    const auto* calendar = deterministic ? std::gmtime(&seconds) : std::localtime(&seconds);
    if (!calendar) return 0;
    return (minutes - calendar->tm_min % minutes - 1) * 60 + 59 - calendar->tm_sec;
}
void Runtime::greenFlag() {
    if (deterministic) {
        now = 0; timerOrigin = 0; nextTask = 1;
        random.seed(randomSeed); randomDraws = 0;
        inputSystem.reset(); inputOrigin = InputOrigin::None;
        executed = 0; bulletClones = 0;
        unsupported.clear(); executedOps.clear(); gameplayEvents.clear();
    }
    tasks.clear(); actors.clear(); stamps.clear(); shownLists.clear(); halted = false; asking = 0; question.clear(); timerOrigin = now;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        auto& d = definitions[index];
        auto a = std::make_shared<Actor>();
        a->type = static_cast<int>(index); a->costume = d.costume; a->layer = d.layer; a->visible = d.visible;
        a->x = d.x; a->y = d.y; a->size = d.size; a->direction = d.direction; a->volume = d.volume;
        a->rotation = d.rotation; a->variables = d.variables; a->lists = d.lists;
        actors.push_back(a);
    }
    resetCameraFromLegacy();
    // Shared cloud snapshots in the supplied archive are stale sessions, not a server.
    for (auto& [id, name] : definitions[0].variableNames) if (name.starts_with("☁")) actors[0]->variables[id] = name == "☁ Online" ? "16" : "";
    for (auto& a : actors) for (std::size_t i = 0; i < definitions[a->type].blocks.size(); ++i)
        if (definitions[a->type].blocks[i].op == "event_whenflagclicked") spawn(a, static_cast<int>(i));
}
std::shared_ptr<Task> Runtime::spawn(std::shared_ptr<Actor> actor, int pc) {
    auto task = std::make_shared<Task>(); task->id = nextTask++; task->actor = std::move(actor); Frame frame; frame.pc = pc; task->frames.push_back(frame); tasks.push_back(task); return task;
}
void Runtime::broadcast(const std::string& message, Task* caller, bool wait) {
    for (auto& a : actors) if (a->alive) {
        auto& blocks = definitions[a->type].blocks;
        for (std::size_t i = 0; i < blocks.size(); ++i) if (blocks[i].op == "event_whenbroadcastreceived" && equal(field(blocks[i], "BROADCAST_OPTION"), message)) {
            // Scratch restarts an already-running receive hat for the same actor.
            for (auto& existing : tasks) if (existing.get() != caller && existing->actor == a && !existing->frames.empty() && existing->frames.front().body == static_cast<int>(i)) existing->alive = false;
            auto task = spawn(a, static_cast<int>(i)); task->frames[0].body = static_cast<int>(i);
            if (caller && wait) caller->waiting.push_back(task->id);
        }
    }
}
std::string& Runtime::variableRef(Actor& a, const std::string& id) {
    if (a.variables.contains(id)) return a.variables[id];
    return actors[0]->variables[id];
}
Value Runtime::variable(Actor& a, const std::string& id) { return variableRef(a, id); }
std::vector<Value>& Runtime::list(Actor& a, const std::string& id) {
    if (a.lists.contains(id)) return a.lists[id];
    return actors[0]->lists[id];
}
void Runtime::setNamed(const std::string& name, const Value& value) {
    for (auto& [id, label] : definitions[0].variableNames) if (label == name) actors[0]->variables[id] = value.text;
}
Value Runtime::namedValue(const std::string& name) const {
    if (!actors.empty()) for (auto& [id, label] : definitions[0].variableNames) if (label == name) { auto it = actors[0]->variables.find(id); if (it != actors[0]->variables.end()) return it->second; }
    return "";
}
void Runtime::startGame() {
    // Explicit offline entry point; this bypasses the intro/menu only.
    for (auto& t : tasks) t->alive = false;
    broadcast("Green Flag");
    broadcast("init");
    for (int i = 0; i < 4; ++i) tick(1.0 / 30);
    setNamed(";SCENE", "game"); setNamed(";Spill-Modus", "4"); setNamed(";KART", "1");
    setNamed("&Screen Length x", "320"); setNamed("&Screen Length y", "180"); setNamed("Screen ratio", "16:9");
    broadcast("Start");
}
int Runtime::listIndex(const Value& value, std::size_t size, bool insertion) {
    if (lower(value.text) == "last") return static_cast<int>(size) - (insertion ? 0 : 1);
    if (lower(value.text) == "random" || lower(value.text) == "any") {
        if (!size) return insertion ? 0 : -1;
        if (deterministic) return static_cast<int>(std::floor(randomUnit() * (size + (insertion ? 1 : 0))));
        return std::uniform_int_distribution<int>(0, static_cast<int>(size) - (insertion ? 0 : 1))(random);
    }
    double n = std::floor(value.number()) - 1;
    if (!std::isfinite(n) || n < 0 || n >= static_cast<double>(size + (insertion ? 1 : 0))) return -1;
    return static_cast<int>(n);
}
bool Runtime::key(const std::string& name, bool pressed) {
    return inputSystem.legacyKeyState(name, pressed);
}
double Runtime::mx() const { return inputSystem.raw().pointer.x; }
double Runtime::my() const { return inputSystem.raw().pointer.y; }
void Runtime::position(Actor& a, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    // Scratch fencing keeps at least 15 pixels of the costume in view.
    auto box = bounds(a);
    double halfW = std::max(0.0, static_cast<double>(box.width) / 2 - 15);
    double halfH = std::max(0.0, static_cast<double>(box.height) / 2 - 15);
    a.x = std::clamp(x, -320 - halfW, 320 + halfW);
    a.y = std::clamp(y, -180 - halfH, 180 + halfH);
}
void Runtime::costume(Actor& a, const Value& value) {
    auto& assets = definitions[a.type].costumes;
    if (assets.empty()) return;
    for (std::size_t i = 0; i < assets.size(); ++i) if (assets[i].name == value.text) { a.costume = static_cast<int>(i); return; }
    double n{};
    if (numeric(value.text, n) && std::isfinite(n)) { int count = static_cast<int>(assets.size()); int index = static_cast<int>(std::fmod(std::floor(n) - 1, count)); a.costume = (index % count + count) % count; }
}
Value Runtime::input(Task& task, const Block& b, const std::string& keyName, int depth) {
    auto found = b.inputs.find(keyName);
    if (found == b.inputs.end()) return "";
    auto& in = found->second;
    if (in.kind == 1) return eval(task, in.block, depth + 1);
    if (in.kind == 2) return variable(*task.actor, in.value);
    if (in.kind == 3) {
        auto& values = list(*task.actor, in.value);
        bool compact = std::all_of(values.begin(), values.end(), [](const Value& value) { return value.text.size() == 1; });
        std::string result;
        for (std::size_t i = 0; i < values.size(); ++i) { if (i && !compact) result += ' '; result += values[i].text; }
        return result;
    }
    return in.value;
}
Value Runtime::eval(Task& task, int index, int depth) {
    if (depth > 128) throw std::runtime_error("Reporter nesting limit");
    auto& a = *task.actor;
    auto& blocks = definitions[a.type].blocks;
    if (index < 0 || index >= static_cast<int>(blocks.size())) return "";
    auto& b = blocks[index]; auto& op = b.op; executedOps.insert(op);
    auto v = [&](const std::string& name) { return input(task, b, name, depth); };
    auto n = [&](const std::string& name) { return v(name).number(); };
    auto boolean = [](bool value) { return Value(value ? "true" : "false"); };
    if (op == "argument_reporter_string_number") { auto name = field(b, "VALUE"); for (auto it = task.frames.rbegin(); it != task.frames.rend(); ++it) if (it->arguments.contains(name)) return it->arguments.at(name); return ""; }
    if (op == "operator_add") return n("NUM1") + n("NUM2");
    if (op == "operator_subtract") return n("NUM1") - n("NUM2");
    if (op == "operator_multiply") return n("NUM1") * n("NUM2");
    if (op == "operator_divide") return n("NUM1") / n("NUM2");
    if (op == "operator_mod") { double x = n("NUM1"), y = n("NUM2"); double r = std::fmod(x, y); return r * y < 0 ? r + y : r; }
    if (op == "operator_round") return std::floor(n("NUM") + 0.5);
    if (op == "operator_equals") return boolean(equal(v("OPERAND1"), v("OPERAND2")));
    if (op == "operator_gt" || op == "operator_lt") { auto x = v("OPERAND1"), y = v("OPERAND2"); double nx{}, ny{}; bool numericBoth = numeric(x.text, nx) && numeric(y.text, ny) && x.text.find_first_not_of(" \t\r\n") != std::string::npos && y.text.find_first_not_of(" \t\r\n") != std::string::npos; return boolean(op == "operator_gt" ? (numericBoth ? nx > ny : lower(x.text) > lower(y.text)) : (numericBoth ? nx < ny : lower(x.text) < lower(y.text))); }
    if (op == "operator_and") return boolean(v("OPERAND1").truth() && v("OPERAND2").truth());
    if (op == "operator_or") return boolean(v("OPERAND1").truth() || v("OPERAND2").truth());
    if (op == "operator_not") return boolean(!v("OPERAND").truth());
    if (op == "operator_join") return v("STRING1").text + v("STRING2").text;
    if (op == "operator_length") return static_cast<double>(v("STRING").text.size());
    if (op == "operator_letter_of") { auto s = v("STRING").text; double i = std::floor(n("LETTER")) - 1; return i >= 0 && i < static_cast<double>(s.size()) ? s.substr(static_cast<std::size_t>(i), 1) : ""; }
    if (op == "operator_contains") return boolean(lower(v("STRING1").text).find(lower(v("STRING2").text)) != std::string::npos);
    if (op == "operator_random") {
        double x = n("FROM");
        double y = n("TO");
        if (x > y) std::swap(x, y);
        if (!std::isfinite(x) || !std::isfinite(y)) return 0.0;
        const bool integers = std::floor(x) == x && std::floor(y) == y;
        double result;
        if (deterministic) result = x + randomUnit() * (y - x + (integers ? 1 : 0));
        else result = std::uniform_real_distribution<double>(x, integers ? y + 1 : y)(random);
        if (integers) result = std::min(y, std::floor(result));
        if (captureEvents) gameplayEvents.push_back({"random", definitions[a.type].name, b.id, "", Value(result).text});
        return result;
    }
    if (op == "operator_mathop") {
        double x = n("NUM"); auto name = field(b, "OPERATOR");
        if (name == "abs") return std::abs(x);
        if (name == "floor") return std::floor(x);
        if (name == "ceiling") return std::ceil(x);
        if (name == "sqrt") return std::sqrt(x);
        if (name == "sin") return std::sin(x * radians);
        if (name == "cos") return std::cos(x * radians);
        if (name == "tan") return std::tan(x * radians);
        if (name == "asin") return std::asin(x) / radians;
        if (name == "acos") return std::acos(x) / radians;
        if (name == "atan") return std::atan(x) / radians;
        if (name == "ln") return std::log(x);
        if (name == "log") return std::log10(x);
        if (name == "e ^") return std::exp(x);
        if (name == "10 ^") return std::pow(10, x);
    }
    if (op == "data_itemoflist" || op == "data_lengthoflist" || op == "data_itemnumoflist" || op == "data_listcontainsitem") {
        // Fields contain display names; resolve to IDs for global/local ownership.
        std::string id = field(b, "LIST_ID");
        if (id.empty()) id = field(b, "LIST");
        auto& values = list(a, id);
        if (op == "data_lengthoflist") return static_cast<double>(values.size());
        if (op == "data_itemoflist") { int i = listIndex(v("INDEX"), values.size()); return i >= 0 ? values[i] : Value(""); }
        auto item = v("ITEM"); auto it = std::find_if(values.begin(), values.end(), [&](const Value& x) { return equal(x, item); });
        if (op == "data_listcontainsitem") return boolean(it != values.end());
        return it == values.end() ? 0.0 : static_cast<double>(it - values.begin() + 1);
    }
    if (op == "motion_xposition") return a.x;
    if (op == "motion_yposition") return a.y;
    if (op == "motion_direction") return a.direction;
    if (op == "looks_size") return a.size;
    if (op == "sound_volume") return a.volume;
    if (op == "looks_costumenumbername") { auto& assets = definitions[a.type].costumes; return field(b, "NUMBER_NAME") == "number" ? Value(a.costume + 1.0) : Value(assets.at(a.costume).name); }
    if (op == "sensing_timer") return now - timerOrigin;
    if (op == "sensing_username") return username;
    if (op == "sensing_mousex") return mx();
    if (op == "sensing_mousey") return my();
    if (op == "sensing_mousedown") return boolean(inputSystem.primaryMouse().held);
    if (op == "sensing_keypressed") return boolean(key(v("KEY_OPTION").text));
    if (op == "sensing_touchingobject") return boolean(touching(a, v("TOUCHINGOBJECTMENU").text));
    if (op == "sensing_distanceto") { auto target = v("DISTANCETOMENU").text; double x = mx(), y = my(); for (auto& actor : actors) if (!actor->clone && definitions[actor->type].name == target) { x = actor->x; y = actor->y; break; } return std::hypot(x - a.x, y - a.y); }
    if (op == "sensing_answer") return answer;
    if (op == "sensing_dayssince2000") { return calendarSeconds() / 86400.0 - 10957; }
    if (op == "sensing_current") { auto time = static_cast<std::time_t>(calendarSeconds()); auto calendar = *(deterministic ? std::gmtime(&time) : std::localtime(&time)); auto name = field(b, "CURRENTMENU"); if (name == "YEAR") return calendar.tm_year + 1900.0; if (name == "MONTH") return calendar.tm_mon + 1.0; if (name == "DATE") return static_cast<double>(calendar.tm_mday); if (name == "DAYOFWEEK") return calendar.tm_wday + 1.0; if (name == "HOUR") return static_cast<double>(calendar.tm_hour); if (name == "MINUTE") return static_cast<double>(calendar.tm_min); return static_cast<double>(calendar.tm_sec); }
    if (op.ends_with("menu") || op == "looks_costume" || op == "looks_backdrops" || op == "sensing_keyoptions") { if (!b.fields.empty()) return b.fields.begin()->second; return ""; }
    if (op == "procedures_prototype") return "";
    unsupported.insert(op); return "";
}

void Runtime::run(Task& task) {
    auto& a = *task.actor;
    auto& d = definitions[a.type];
    auto push = [&](int pc) { if (pc >= 0) { Frame f; f.pc = pc; task.frames.push_back(std::move(f)); } };
    auto resolve = [&](const std::string& name, bool isList) {
        for (int type : {a.type, 0}) { auto& names = isList ? definitions[type].listNames : definitions[type].variableNames; for (auto& [id, label] : names) if (label == name) return id; }
        return name;
    };
    for (int budget = 0; budget < 50000 && task.alive && a.alive && !halted; ++budget) {
        if (task.frames.empty()) { task.alive = false; break; }
        auto& frame = task.frames.back();
        if (frame.kind != 0) {
            bool again = frame.kind == 2 || (frame.kind == 1 && frame.remaining-- > 0);
            if (frame.kind == 3) again = !input(task, d.blocks.at(frame.pc), "CONDITION").truth();
            if (!again) { task.frames.pop_back(); continue; }
            int body = frame.body; push(body);
            bool warp = std::any_of(task.frames.begin(), task.frames.end(), [](const Frame& f) { return f.warp; });
            if (!warp) return;
            continue;
        }
        if (frame.pc < 0) { task.frames.pop_back(); continue; }
        if (frame.pc >= static_cast<int>(d.blocks.size())) throw std::runtime_error("Invalid program counter");
        int current = frame.pc;
        auto& b = d.blocks[current]; auto& op = b.op;
        frame.pc = b.next; ++executed; executedOps.insert(op);
        auto v = [&](const std::string& name) { return input(task, b, name); };
        auto n = [&](const std::string& name) { return v(name).number(); };
        auto body = [&](const std::string& name) { auto it = b.inputs.find(name); return it == b.inputs.end() ? -1 : it->second.block; };
        if (op.starts_with("event_when") || op == "control_start_as_clone") continue;
        if (op != "procedures_call" && runNativeMovement(task, b, frame)) continue;
        if (op == "control_if" || op == "control_if_else") { bool yes = v("CONDITION").truth(); if (yes || op == "control_if_else") push(body(yes ? "SUBSTACK" : "SUBSTACK2")); continue; }
        if (op == "control_repeat" || op == "control_forever" || op == "control_repeat_until") {
            Frame loop; loop.kind = op == "control_repeat" ? 1 : op == "control_forever" ? 2 : 3;
            loop.pc = current; loop.body = body("SUBSTACK"); loop.remaining = std::max(0.0, std::floor(n("TIMES") + 0.5)); task.frames.push_back(loop); continue;
        }
        if (op == "control_wait") { task.wake = now + std::max(0.0, n("DURATION")); return; }
        if (op == "event_broadcast" || op == "event_broadcastandwait") { broadcast(v("BROADCAST_INPUT").text, &task, op == "event_broadcastandwait"); if (!task.waiting.empty()) return; continue; }
        if (op == "procedures_call") {
            auto it = d.procedures.find(b.procedure);
            if (it == d.procedures.end()) throw std::runtime_error("Missing procedure: " + b.procedure);
            auto& definition = d.blocks[it->second]; auto& prototype = d.blocks.at(definition.inputs.at("custom_block").block);
            Frame call; call.pc = definition.next; call.warp = prototype.warp; call.procedure = true;
            for (auto& [id, name] : prototype.arguments) call.arguments[name] = v(id).text;
            if (runNativeMovement(task, b, call)) continue;
            if (task.frames.size() > 512) throw std::runtime_error("Procedure stack limit");
            task.frames.push_back(std::move(call)); continue;
        }
        if (op == "control_stop") {
            auto option = field(b, "STOP_OPTION");
            if (option == "all") { halted = true; return; }
            if (option == "this script") {
                // Scratch VM unwinds to the nearest procedure call, if present.
                while (!task.frames.empty() && !task.frames.back().procedure) task.frames.pop_back();
                if (!task.frames.empty()) { task.frames.pop_back(); continue; }
                task.alive = false; return;
            }
            for (auto& other : tasks) if (other.get() != &task && other->actor == task.actor) other->alive = false;
            continue;
        }
        if (op == "control_delete_this_clone") { if (a.clone) { a.alive = false; task.alive = false; return; } continue; }
        if (op == "control_create_clone_of") {
            if (actors.size() >= 315) continue;
            auto name = v("CLONE_OPTION").text; auto source = task.actor;
            if (name != "_myself_") { source.reset(); for (auto& actor : actors) if (!actor->clone && definitions[actor->type].name == name) { source = actor; break; } }
            if (source) {
                auto clone = std::make_shared<Actor>(*source); clone->clone = true; actors.push_back(clone);
                if (definitions[clone->type].name == "[Bullet]") for (auto& [id, name] : definitions[clone->type].variableNames) if (name == "__ID" && clone->variables[id] == "bullet") { ++bulletClones; if (captureEvents) gameplayEvents.push_back({"shot", d.name, b.id, "", "bullet"}); break; }
                auto& blocks = definitions[clone->type].blocks;
                for (std::size_t i = 0; i < blocks.size(); ++i) if (blocks[i].op == "control_start_as_clone") spawn(clone, static_cast<int>(i));
            }
            continue;
        }
        if (op == "data_setvariableto" || op == "data_changevariableby") {
            auto id = field(b, "VARIABLE_ID");
            if (id.empty()) id = resolve(field(b, "VARIABLE"), false);
            auto& ref = variableRef(a, id);
            if (writeNativeCamera(task, b, id, ref)) continue;
            auto value = v("VALUE");
            auto before = ref;
            ref = op == "data_changevariableby" ? Value(Value(ref).number() + value.number()).text : value.text;
            syncCameraField(id, Value(ref).number());
            if (captureEvents && before != ref && definitions[0].variableNames.contains(id)) {
                const auto& name = definitions[0].variableNames.at(id);
                if (name == "::Spiller-liv" || name == "::Ammonisjon" ||
                    name == "::Våpen holdt" || name == ";SCENE" || name == ";Spill-Modus" ||
                    name == "::Player game-points" || name == "::[Spiller-action]") {
                    gameplayEvents.push_back({name, d.name, b.id, before, ref});
                }
            }
            continue;
        }
        if (op.starts_with("data_")) {
            if (op == "data_hidevariable") continue;
            auto id = field(b, "LIST_ID"); if (id.empty()) id = resolve(field(b, "LIST"), true); auto& values = list(a, id);
            if (op == "data_showlist") { shownLists.insert(id); continue; }
            if (op == "data_hidelist") { shownLists.erase(id); continue; }
            if (op == "data_deletealloflist") { values.clear(); continue; }
            if (op == "data_addtolist") { if (values.size() < 200000) values.push_back(v("ITEM")); continue; }
            auto indexValue = v("INDEX"); int index = listIndex(indexValue, values.size());
            if (op == "data_deleteoflist") { if (indexValue.text == "all") values.clear(); else if (index >= 0) values.erase(values.begin() + index); continue; }
            if (op == "data_replaceitemoflist") { if (index >= 0) values[index] = v("ITEM"); continue; }
        }
        if (op == "motion_gotoxy") { position(a, n("X"), n("Y")); continue; }
        if (op == "motion_setx" || op == "motion_changexby") { position(a, op == "motion_setx" ? n("X") : a.x + n("DX"), a.y); continue; }
        if (op == "motion_sety" || op == "motion_changeyby") { position(a, a.x, op == "motion_sety" ? n("Y") : a.y + n("DY")); continue; }
        if (op == "motion_movesteps") { double steps = n("STEPS"); position(a, a.x + std::sin(a.direction * radians) * steps, a.y + std::cos(a.direction * radians) * steps); continue; }
        if (op == "motion_pointindirection" || op == "motion_turnright") { double direction = op == "motion_pointindirection" ? n("DIRECTION") : a.direction + n("DEGREES"); if (std::isfinite(direction)) a.direction = std::remainder(direction, 360); continue; }
        if (op == "motion_setrotationstyle") { a.rotation = field(b, "STYLE"); continue; }
        if (op == "looks_show" || op == "looks_hide") { a.visible = op == "looks_show"; continue; }
        if (op == "looks_switchcostumeto") { costume(a, v("COSTUME")); continue; }
        if (op == "looks_nextcostume") { costume(a, a.costume + 2.0); continue; }
        if (op == "looks_switchbackdropto") { costume(*actors[0], v("BACKDROP")); continue; }
        if (op == "looks_setsizeto" || op == "looks_changesizeby") { double size = op == "looks_setsizeto" ? n("SIZE") : a.size + n("CHANGE"); if (std::isfinite(size)) a.size = std::clamp(size, 0.01, 10000.0); continue; }
        if (op == "looks_cleargraphiceffects") { a.effects.clear(); continue; }
        if (op == "looks_seteffectto" || op == "looks_changeeffectby") { auto name = lower(field(b, "EFFECT")); double value = n(op == "looks_seteffectto" ? "VALUE" : "CHANGE"); a.effects[name] = op == "looks_seteffectto" ? value : a.effects[name] + value; continue; }
        if (op == "looks_gotofrontback") { int layer = a.layer; for (auto& actor : actors) layer = field(b, "FRONT_BACK") == "front" ? std::max(layer, actor->layer + 1) : std::min(layer, actor->layer - 1); a.layer = layer; continue; }
        if (op == "looks_goforwardbackwardlayers") { a.layer += static_cast<int>(n("NUM")) * (field(b, "FORWARD_BACKWARD") == "forward" ? 1 : -1); continue; }
        if (op == "looks_say" || op == "looks_think") { a.bubble = v("MESSAGE").text; continue; }
        if (op == "pen_clear") { stamps.clear(); continue; }
        if (op == "pen_stamp") { stamps.push_back(std::make_shared<Actor>(a)); continue; }
        if (op == "sensing_resettimer") { timerOrigin = now; continue; }
        if (op == "sensing_askandwait") { if (headless) { answer.clear(); continue; } question = v("QUESTION").text; answer.clear(); asking = task.id; task.wake = std::numeric_limits<double>::infinity(); return; }
        if (op == "sound_setvolumeto" || op == "sound_changevolumeby") { a.volume = std::clamp(op == "sound_setvolumeto" ? n("VOLUME") : a.volume + n("VOLUME"), 0.0, 100.0); continue; }
        if (op == "sound_seteffectto" || op == "sound_cleareffects") { if (op == "sound_cleareffects") { a.effects.erase("pitch"); a.effects.erase("pan"); } else a.effects[lower(field(b, "EFFECT"))] = n("VALUE"); continue; }
        if (op == "sound_stopallsounds") { for (auto& [name, sound] : sounds) StopSound(sound); continue; }
        if (op == "sound_play") {
            if (!headless && !mute && IsAudioDeviceReady()) playActorSound(a, v("SOUND_MENU"));
            continue;
        }
        unsupported.insert(op);
        throw std::runtime_error("Unsupported statement " + op + " in " + d.name + " block " + b.id);
    }
}

void Runtime::playActorSound(Actor& a, const Value& wanted) {
    const auto& d = definitions[a.type];
    if (headless || mute || !IsAudioDeviceReady()) return;
    const Asset* asset = nullptr;
    for (auto& item : d.sounds) if (item.name == wanted.text) { asset = &item; break; }
    if (!asset) { int i = listIndex(wanted, d.sounds.size()); if (i >= 0) asset = &d.sounds[i]; }
    if (asset) { auto it = sounds.find(asset->file); if (it == sounds.end()) it = sounds.emplace(asset->file, loadPackagedSound(asset->file)).first; if (it->second.frameCount) { SetSoundVolume(it->second, static_cast<float>(a.volume / 100)); SetSoundPitch(it->second, static_cast<float>(std::pow(2, a.effects["pitch"] / 120))); SetSoundPan(it->second, static_cast<float>((a.effects["pan"] + 100) / 200)); PlaySound(it->second); } }
}

void Runtime::tick(double dt) {
    gameplayEvents.clear();
    now += dt;
    updatePlayerInput();
    if (inputOrigin == InputOrigin::Replay && inputSystem.raw().wheel > 0) broadcast("scroll up");
    if (inputOrigin == InputOrigin::Replay && inputSystem.raw().wheel < 0) broadcast("scroll down");
    if (halted) return;
    if (!headless || inputOrigin == InputOrigin::Replay) {
        if (asking && !headless) { int c; while ((c = gfx::nextCharacter()) > 0) if (c >= 32 && c < 127) answer += static_cast<char>(c); if (gfx::keyPressed(KEY_BACKSPACE) && !answer.empty()) answer.pop_back(); if (gfx::keyPressed(KEY_ENTER)) { for (auto& task : tasks) if (task->id == asking) task->wake = now; asking = 0; question.clear(); } }
        auto snapshot = actors;
        for (auto& a : snapshot) if (a->alive) {
            auto& blocks = definitions[a->type].blocks;
            for (std::size_t i = 0; i < blocks.size(); ++i) if (blocks[i].op == "event_whenkeypressed" && key(field(blocks[i], "KEY_OPTION"), true)) spawn(a, static_cast<int>(i));
        }
        if (inputOrigin == InputOrigin::Live && inputSystem.raw().wheel > 0) broadcast("scroll up");
        if (inputOrigin == InputOrigin::Live && inputSystem.raw().wheel < 0) broadcast("scroll down");
    }
    // Newly broadcast hats run this tick; loop yields resume on the next tick.
    // Bound task count as well as per-task work to avoid accidental infinite dispatch.
    for (std::size_t i = 0; i < tasks.size() && i < 10000; ++i) {
        auto task = tasks[i];
        if (!task->alive || !task->actor->alive || task->wake > now) continue;
        std::erase_if(task->waiting, [&](std::uint64_t id) { return std::none_of(tasks.begin(), tasks.end(), [&](const auto& other) { return other->id == id && other->alive && other->actor->alive; }); });
        if (!task->waiting.empty()) continue;
        run(*task);
    }
    std::erase_if(tasks, [](auto& task) { return !task->alive || !task->actor->alive; });
    std::erase_if(actors, [](auto& actor) { return !actor->alive; });
    if (tasks.size() > 10000) throw std::runtime_error("Task dispatch limit");
    if (stamps.size() > 10000) throw std::runtime_error("Pen stamp limit");
}

CachedImage* Runtime::image(Actor& a) {
    auto& assets = definitions[a.type].costumes;
    if (a.costume < 0 || a.costume >= static_cast<int>(assets.size())) return nullptr;
    auto& asset = assets[a.costume]; auto it = images.find(asset.file);
    if (it != images.end()) return &it->second;
    auto path = std::filesystem::path(asset.file);
    auto bytes = readAsset(path.generic_string());
    CachedImage cached; cached.image = LoadImageFromMemory(path.extension().string().c_str(), bytes.data(), static_cast<int>(bytes.size()));
    if (!cached.image.data) throw std::runtime_error("Cannot decode costume " + path.string() + "; run tools/build.ps1");
    ImageFormat(&cached.image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (!headless) { cached.texture = gfx::upload(cached.image); }
    return &images.emplace(asset.file, cached).first->second;
}
Rectangle Runtime::bounds(Actor& a) {
    auto* cached = image(a);
    if (!cached) return {};
    auto& asset = definitions[a.type].costumes[a.costume];
    double scale = a.size / 100 / asset.resolution;
    double angle = a.rotation == "all around" ? (a.direction - 90) * radians : 0;
    double sign = a.rotation == "left-right" && a.direction < 0 ? -1 : 1;
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (double x : {0.0, static_cast<double>(cached->image.width)}) for (double y : {0.0, static_cast<double>(cached->image.height)}) {
        double dx = (x - asset.cx) * scale * sign, dy = (y - asset.cy) * scale;
        double sx = a.x + dx * std::cos(angle) - dy * std::sin(angle), sy = -a.y + dx * std::sin(angle) + dy * std::cos(angle);
        minx = std::min(minx, sx); maxx = std::max(maxx, sx); miny = std::min(miny, sy); maxy = std::max(maxy, sy);
    }
    return {static_cast<float>(minx), static_cast<float>(miny), static_cast<float>(maxx - minx), static_cast<float>(maxy - miny)};
}
bool Runtime::opaque(Actor& a, double x, double y) {
    auto* cached = image(a); if (!cached) return false;
    auto& asset = definitions[a.type].costumes[a.costume]; double scale = a.size / 100 / asset.resolution;
    if (scale <= 0) return false;
    double angle = a.rotation == "all around" ? (a.direction - 90) * radians : 0;
    double dx = x - a.x, dy = y + a.y;
    double px = (dx * std::cos(angle) + dy * std::sin(angle)) / scale;
    if (a.rotation == "left-right" && a.direction < 0) px = -px;
    int ix = static_cast<int>(std::floor(px + asset.cx)), iy = static_cast<int>(std::floor((-dx * std::sin(angle) + dy * std::cos(angle)) / scale + asset.cy));
    if (ix < 0 || iy < 0 || ix >= cached->image.width || iy >= cached->image.height) return false;
    return static_cast<const Color*>(cached->image.data)[iy * cached->image.width + ix].a > 0;
}
bool Runtime::touching(Actor& a, const std::string& target) {
    if (target == "_mouse_") return opaque(a, mx(), -my());
    auto box = bounds(a);
    if (target == "_edge_") return box.x <= -320 || box.x + box.width >= 320 || box.y <= -180 || box.y + box.height >= 180;
    for (auto& other : actors) if (other.get() != &a && other->alive && other->visible && definitions[other->type].name == target) {
        auto otherBox = bounds(*other); if (!CheckCollisionRecs(box, otherBox)) continue;
        auto overlap = GetCollisionRec(box, otherBox);
        // Sample at native stage pixel centers, matching costume alpha masks.
        for (int y = static_cast<int>(std::floor(overlap.y)); y < std::ceil(overlap.y + overlap.height); ++y)
            for (int x = static_cast<int>(std::floor(overlap.x)); x < std::ceil(overlap.x + overlap.width); ++x)
                if (opaque(a, x + 0.5, y + 0.5) && opaque(*other, x + 0.5, y + 0.5)) return true;
    }
    return false;
}
void Runtime::paint(Actor& a) {
    auto* cached = image(a); if (!cached || !cached->texture.id) return;
    auto& asset = definitions[a.type].costumes[a.costume]; float scale = static_cast<float>(a.size / 100 / asset.resolution);
    Rectangle source{0, 0, static_cast<float>(cached->image.width), static_cast<float>(cached->image.height)};
    float cx = static_cast<float>(asset.cx);
    if (a.rotation == "left-right" && a.direction < 0) { source.width = -source.width; cx = cached->image.width - cx; }
    Color tint = WHITE;
    if (a.effects.contains("ghost")) tint.a = static_cast<unsigned char>(255 * (1 - std::clamp(a.effects["ghost"], 0.0, 100.0) / 100));
    float hue = static_cast<float>(a.effects["color"] / 200), brightness = static_cast<float>(std::clamp(a.effects["brightness"], -100.0, 100.0) / 100);
    const auto logical = Viewport{}.stageToLogical({a.x, a.y});
    gfx::sprite(cached->texture, source,
                {static_cast<float>(logical.x), static_cast<float>(logical.y),
                 cached->image.width * scale, cached->image.height * scale},
                {cx * scale, static_cast<float>(asset.cy) * scale},
                a.rotation == "all around" ? static_cast<float>(a.direction - 90) : 0,
                tint, hue, brightness);
}
void Runtime::draw() {
    if (headless) return;
    if (!actors.empty()) paint(*actors[0]);
    const auto scene = namedValue(";SCENE").text;
    const auto worldArt = [&](const Actor& actor) {
        const auto& definition = definitions[actor.type];
        if (definition.name == "[Introduction]" || definition.name == "Graphics")
            return false;
        if (definition.name == "[UI]") return scene != "game";
        const bool hasCostume = actor.costume >= 0 &&
            static_cast<std::size_t>(actor.costume) < definition.costumes.size();
        if (hasCostume) {
            const auto& name = definition.costumes[actor.costume].name;
            if (name == "Crosshair" || name == "lonely game") return false;
        }
        if (definition.name != "[GAME UI]") return true;
        for (const auto& [id, name] : definition.variableNames) {
            if (name != "_ID") continue;
            const auto found = actor.variables.find(id);
            if (found == actor.variables.end()) return true;
            const auto& role = found->second;
            return role != "healthbar" && role != "ammo" && role != "kills" &&
                   role != "killcount" && role != "gamemode" && role != "heli-text" &&
                   role != "players" && (role != "countdown" || scene != "game");
        }
        return true;
    };
    for (auto& stamp : stamps) if (worldArt(*stamp)) paint(*stamp);
    auto sorted = actors;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x->layer < y->layer; });
    for (auto& actor : sorted) if (actor->type != 0 && actor->alive && actor->visible && worldArt(*actor)) { paint(*actor); if (!actor->bubble.empty()) gfx::text(actor->bubble.c_str(), static_cast<float>(actor->x + 320), static_cast<float>(160 - actor->y), 10, WHITE); }
    int row = 25;
    for (auto& id : shownLists) { std::string text; for (auto& actor : actors) if (actor->lists.contains(id)) for (auto& value : actor->lists[id]) text += value.text + "\n"; gfx::rectangle(8, row, 450, 70, {35, 40, 50, 230}); gfx::text(text.c_str(), 12, row + 5, 10, WHITE); row += 75; }
    if (asking) { gfx::rectangle(10, 280, 460, 70, DARKGRAY); gfx::text(question.c_str(), 15, 287, 10, WHITE); gfx::text(answer.c_str(), 15, 310, 12, WHITE); }
    drawGameHud(*this);
    if (halted) gfx::text("Scripts stopped | F5 restart | F6 offline game", 10, 345, 8, ORANGE);
}
std::string Runtime::diagnostics() const {
    std::ostringstream out; out << "C++26 runtime: " << definitions.size() << " targets, " << actors.size() << " actors, " << tasks.size() << " tasks; " << executed << " statements; " << executedOps.size() << " opcodes exercised; stopped=" << halted << "; unsupported=" << unsupported.size();
    out << "; bullet clones=" << bulletClones;
    for (auto& op : unsupported) out << '\n' << op;
    if (!actors.empty()) for (auto& [id, name] : definitions[0].variableNames) if (name == ";SCENE" || name == "::Player X" || name == "::Player Y" || name == "::Spiller-liv") { auto it = actors[0]->variables.find(id); if (it != actors[0]->variables.end()) out << "; " << name << '=' << it->second; }
    return out.str();
}
int selfTest() {
    testInputSystem();
    testCameraSystem();
    testMovement();
    auto require = [](bool pass, const char* message) { if (!pass) throw std::runtime_error(message); };
    content::validate();
    require(content::items().size() == 26 && content::matches().size() == 4,
            "Complete typed content catalog");
    auto smg = content::findItem("d");
    require(smg && content::item(*smg).gearPoints == 500 &&
                content::item(*smg).reloadSeconds == 2.0,
            "SMG source tuning preserved");
    require(!content::findItem("invalid"), "Unknown legacy code has no silent fallback");
    require(!content::item(content::ItemId::Bow).reloadSeconds &&
                !content::item(content::ItemId::Helicopter).gearPoints,
            "Missing source values remain missing");
    require(equal("01", "1"), "Numeric Scratch comparison");
    require(equal("HELLO", "hello"), "Case-insensitive comparison");
    require(!equal("", "0"), "Empty strings compare lexically");
    require(Value("abc").number() == 0, "Non-numeric arithmetic casts to zero");
    require(!Value("false").truth() && !Value("0").truth() && Value("00").truth(), "Scratch booleans");
    require(Value(-1.25).text == "-1.25", "Stable numeric formatting");
    {
        Runtime runtime(".", true);
        Definition definition;
        definition.name = "Test stage";
        definition.variables["counter"] = "0";
        definition.variableNames["counter"] = "counter";
        Block call; call.op = "procedures_call"; call.procedure = "early return"; call.next = 1;
        Block after; after.op = "data_setvariableto"; after.fields = {{"VARIABLE_ID", "counter"}}; after.inputs["VALUE"] = {0, -1, "42"};
        Block definitionBlock; definitionBlock.op = "procedures_definition"; definitionBlock.inputs["custom_block"] = {1, 3, ""}; definitionBlock.next = 4;
        Block prototype; prototype.op = "procedures_prototype"; prototype.procedure = "early return"; prototype.warp = true;
        Block stop; stop.op = "control_stop"; stop.fields = {{"STOP_OPTION", "this script"}};
        definition.blocks = {call, after, definitionBlock, prototype, stop}; definition.procedures["early return"] = 2;
        runtime.definitions.push_back(definition); runtime.greenFlag();
        runtime.spawn(runtime.actors[0], 0); runtime.tick(1.0 / 30);
        require(runtime.actors[0]->variables["counter"] == "42", "Stop this script returns from a custom procedure");
        auto& blocks = runtime.definitions[0].blocks;
        Block repeat; repeat.op = "control_repeat"; repeat.inputs["TIMES"] = {0, -1, "3"}; repeat.inputs["SUBSTACK"] = {1, 6, ""};
        Block increment; increment.op = "data_changevariableby"; increment.fields = {{"VARIABLE_ID", "counter"}}; increment.inputs["VALUE"] = {0, -1, "1"};
        blocks.push_back(repeat); blocks.push_back(increment);
        runtime.spawn(runtime.actors[0], 5);
        for (int i = 0; i < 5; ++i) runtime.tick(1.0 / 30);
        require(runtime.actors[0]->variables["counter"] == "45", "Repeat executes exact number of iterations");
        require(runtime.tasks.empty(), "Completed tasks are retired");
        Block clone; clone.op = "control_create_clone_of"; clone.inputs["CLONE_OPTION"] = {0, -1, "_myself_"};
        Block cloneHat; cloneHat.op = "control_start_as_clone"; cloneHat.next = 9;
        blocks.push_back(clone); blocks.push_back(cloneHat); blocks.push_back(increment);
        runtime.spawn(runtime.actors[0], 7); runtime.tick(1.0 / 30);
        require(runtime.actors.size() == 2 && runtime.actors[1]->clone, "Clone creation starts clone hats");
        require(runtime.actors[0]->variables["counter"] == "45" && runtime.actors[1]->variables["counter"] == "46", "Clones own independent local variables");
        Block receive; receive.op = "event_whenbroadcastreceived"; receive.fields["BROADCAST_OPTION"] = "test"; receive.next = 11;
        Block update = after; update.inputs["VALUE"].value = "99";
        Block send; send.op = "event_broadcastandwait"; send.inputs["BROADCAST_INPUT"] = {0, -1, "test"}; send.next = 13;
        Block continuation = after; continuation.inputs["VALUE"].value = "100";
        blocks.push_back(receive); blocks.push_back(update); blocks.push_back(send); blocks.push_back(continuation);
        runtime.spawn(runtime.actors[0], 12); runtime.tick(1.0 / 30);
        require(runtime.actors[0]->variables["counter"] == "99" && runtime.actors[1]->variables["counter"] == "99", "Broadcast reaches originals and clones before wait continuation");
        runtime.tick(1.0 / 30);
        require(runtime.actors[0]->variables["counter"] == "100", "Broadcast-and-wait resumes after receiver completion");
    }
    std::cout << "Scratch values, procedure returns, loops, clones, broadcasts and task retirement passed\n";
    return 0;
}
}
