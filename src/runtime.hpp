#pragma once
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bb {
using Fields = std::unordered_map<std::string, std::string>;
struct Value {
    std::string text;
    Value() = default;
    Value(std::string s) : text(std::move(s)) {}
    Value(const char* s) : text(s) {}
    Value(double n);
    double number() const;
    bool truth() const;
};
bool equal(const Value& a, const Value& b);
struct Input { int kind{}, block{-1}; std::string value; };
struct Block {
    std::string id, op, procedure;
    int next{-1}; bool top{}, warp{};
    Fields fields;
    std::vector<std::pair<std::string, std::string>> arguments;
    std::unordered_map<std::string, Input> inputs;
};
struct Asset { std::string name, file; double cx{}, cy{}, resolution{1}; };
struct Definition {
    std::string name, rotation;
    double x{}, y{}, size{100}, direction{90}, volume{100};
    int costume{}, layer{}; bool visible{};
    Fields variables, variableNames, listNames;
    std::unordered_map<std::string, std::vector<Value>> lists;
    std::vector<Asset> costumes, sounds;
    std::vector<Block> blocks;
    std::unordered_map<std::string, int> procedures;
};
struct Actor {
    int type{}, costume{}, layer{}; bool clone{}, alive{true}, visible{};
    double x{}, y{}, size{100}, direction{90}, volume{100};
    std::string rotation, bubble;
    Fields variables;
    std::unordered_map<std::string, std::vector<Value>> lists;
    std::unordered_map<std::string, double> effects;
};
struct Frame {
    int pc{-1}, body{-1}, kind{};
    double remaining{}; bool warp{}, procedure{};
    Fields arguments;
};
struct Task {
    std::uint64_t id{};
    std::shared_ptr<Actor> actor;
    std::vector<Frame> frames;
    std::vector<std::uint64_t> waiting;
    double wake{}; bool alive{true};
};
struct CachedImage { Image image{}; Texture2D texture{}; };
class Runtime {
public:
    explicit Runtime(std::filesystem::path root, bool headless = false);
    ~Runtime();
    void load();
    void greenFlag();
    void tick(double dt);
    void draw();
    void broadcast(const std::string& message, Task* caller = nullptr, bool wait = false);
    void startGame();
    std::string diagnostics() const;
    bool stopped() const { return halted; }
    Value namedValue(const std::string& name) const;
    std::size_t bulletClones{};
    std::size_t executed{};
    std::string username{"DX_Player"};
    bool mute{true};
    bool scriptedInput{}, scriptedMouse{};
    std::set<std::string> scriptedKeys;
    std::vector<Definition> definitions;
    std::vector<std::shared_ptr<Actor>> actors;
    std::set<std::string> unsupported;
    std::set<std::string> executedOps;
private:
    friend int selfTest();
    std::filesystem::path root;
    bool headless{}, halted{};
    double now{}, timerOrigin{};
    std::uint64_t nextTask{1};
    std::vector<std::shared_ptr<Task>> tasks;
    std::unordered_map<std::string, CachedImage> images;
    std::unordered_map<std::string, Sound> sounds;
    std::mt19937 random{0xBBD};
    std::vector<std::shared_ptr<Actor>> stamps;
    std::string answer, question;
    std::uint64_t asking{};
    std::set<std::string> shownLists;
    std::shared_ptr<Task> spawn(std::shared_ptr<Actor> actor, int pc);
    void run(Task& task);
    Value eval(Task& task, int block, int depth = 0);
    Value input(Task& task, const Block& block, const std::string& key, int depth = 0);
    Value variable(Actor& actor, const std::string& id);
    std::string& variableRef(Actor& actor, const std::string& id);
    std::vector<Value>& list(Actor& actor, const std::string& id);
    int listIndex(const Value& index, std::size_t size, bool insertion = false);
    bool key(const std::string& name, bool pressed = false);
    void position(Actor& actor, double x, double y);
    void costume(Actor& actor, const Value& value);
    bool touching(Actor& actor, const std::string& target);
    CachedImage* image(Actor& actor);
    Rectangle bounds(Actor& actor);
    bool opaque(Actor& actor, double x, double y);
    void paint(Actor& actor);
    double mx() const;
    double my() const;
    void setNamed(const std::string& name, const Value& value);
};
int selfTest();
}
