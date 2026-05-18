#include "SceneSerializer.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"
#include "Log.h"
#include "../Renderer/Camera.h"
#include "../Physics/Rigidbody.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <cctype>
#include <algorithm>

using namespace DirectX;

namespace VibeEngine {

// ============================================================================
// Minimal self-contained JSON library (writer + recursive-descent parser)
// No heap-allocating containers for keys — uses linear-scan ordered vectors.
// ============================================================================
namespace Json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;

    bool        bval = false;
    double      nval = 0.0;
    std::string sval;

    // Array items (order preserved)
    std::vector<Value>                          aval;
    // Object members (insertion order preserved; linear search by key)
    std::vector<std::pair<std::string, Value>>  oval;

    // ---- Factory helpers ----------------------------------------------------
    static Value Null()                        { return {}; }
    static Value Bool(bool b)                  { Value v; v.type = Type::Bool;   v.bval = b;  return v; }
    static Value Number(double n)              { Value v; v.type = Type::Number; v.nval = n;  return v; }
    static Value Str(const std::string& s)     { Value v; v.type = Type::String; v.sval = s;  return v; }
    static Value Array()                       { Value v; v.type = Type::Array;               return v; }
    static Value Object()                      { Value v; v.type = Type::Object;              return v; }

    // ---- Object helpers -----------------------------------------------------
    void Set(const std::string& key, Value val) {
        for (auto& [k, v] : oval) {
            if (k == key) { v = std::move(val); return; }
        }
        oval.emplace_back(key, std::move(val));
    }
    const Value* Get(const std::string& key) const {
        for (auto& [k, v] : oval)
            if (k == key) return &v;
        return nullptr;
    }

    // ---- Array helpers ------------------------------------------------------
    void Push(Value val) { aval.push_back(std::move(val)); }

    // ---- Typed getters with defaults ----------------------------------------
    bool        AsBool  (bool   def = false) const { return type == Type::Bool   ? bval : def; }
    double      AsDouble(double def = 0.0  ) const { return type == Type::Number ? nval : def; }
    float       AsFloat (float  def = 0.f  ) const { return type == Type::Number ? static_cast<float>(nval) : def; }
    int         AsInt   (int    def = 0    ) const { return type == Type::Number ? static_cast<int>(nval) : def; }
    const std::string& AsString() const {
        static const std::string empty;
        return type == Type::String ? sval : empty;
    }
};

// ============================================================================
// Writer
// ============================================================================
namespace {

void WriteIndent(std::string& out, int depth) {
    for (int i = 0; i < depth * 2; ++i) out += ' ';
}

void WriteEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += static_cast<char>(c); break;
        }
    }
    out += '"';
}

void WriteValue(std::string& out, const Value& val, int depth = 0) {
    switch (val.type) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += val.bval ? "true" : "false";
        break;
    case Type::Number: {
        char buf[64];
        double d = val.nval;
        // Always emit at least one decimal place for floats
        if (d == static_cast<double>(static_cast<long long>(d)))
            std::snprintf(buf, sizeof(buf), "%.1f", d);
        else
            std::snprintf(buf, sizeof(buf), "%.8g", d);
        out += buf;
        break;
    }
    case Type::String:
        WriteEscaped(out, val.sval);
        break;
    case Type::Array:
        if (val.aval.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < val.aval.size(); ++i) {
            WriteIndent(out, depth + 1);
            WriteValue(out, val.aval[i], depth + 1);
            if (i + 1 < val.aval.size()) out += ',';
            out += '\n';
        }
        WriteIndent(out, depth);
        out += ']';
        break;
    case Type::Object:
        if (val.oval.empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t i = 0; i < val.oval.size(); ++i) {
            WriteIndent(out, depth + 1);
            WriteEscaped(out, val.oval[i].first);
            out += ": ";
            WriteValue(out, val.oval[i].second, depth + 1);
            if (i + 1 < val.oval.size()) out += ',';
            out += '\n';
        }
        WriteIndent(out, depth);
        out += '}';
        break;
    }
}

} // anonymous namespace

static std::string Stringify(const Value& root) {
    std::string out;
    out.reserve(4096);
    WriteValue(out, root);
    return out;
}

// ============================================================================
// Parser — recursive descent
// ============================================================================
struct Parser {
    const char* p;
    const char* end;

    explicit Parser(const std::string& s)
        : p(s.data()), end(s.data() + s.size()) {}

    void SkipWS() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }

    bool Accept(char c) {
        SkipWS();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    Value ParseValue() {
        SkipWS();
        if (p >= end)    return Value::Null();
        if (*p == '{')   return ParseObject();
        if (*p == '[')   return ParseArray();
        if (*p == '"')   return ParseString();
        if (*p == 't')   { p += 4; return Value::Bool(true);  }
        if (*p == 'f')   { p += 5; return Value::Bool(false); }
        if (*p == 'n')   { p += 4; return Value::Null();      }
        return ParseNumber();
    }

    Value ParseObject() {
        Value v = Value::Object();
        ++p; // consume '{'
        SkipWS();
        if (p < end && *p == '}') { ++p; return v; }
        while (p < end) {
            SkipWS();
            if (p >= end || *p != '"') break;
            Value ks = ParseString();
            if (!Accept(':')) break;
            v.oval.emplace_back(ks.sval, ParseValue());
            SkipWS();
            if (p < end && *p == ',') { ++p; continue; }
            break;
        }
        Accept('}');
        return v;
    }

    Value ParseArray() {
        Value v = Value::Array();
        ++p; // consume '['
        SkipWS();
        if (p < end && *p == ']') { ++p; return v; }
        while (p < end) {
            v.aval.push_back(ParseValue());
            SkipWS();
            if (p < end && *p == ',') { ++p; continue; }
            break;
        }
        Accept(']');
        return v;
    }

    Value ParseString() {
        Value v;
        v.type = Type::String;
        ++p; // consume opening '"'
        while (p < end && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (p >= end) break;
                switch (*p) {
                case '"':  v.sval += '"';  break;
                case '\\': v.sval += '\\'; break;
                case '/':  v.sval += '/';  break;
                case 'n':  v.sval += '\n'; break;
                case 'r':  v.sval += '\r'; break;
                case 't':  v.sval += '\t'; break;
                default:   v.sval += *p;   break;
                }
            } else {
                v.sval += *p;
            }
            ++p;
        }
        if (p < end) ++p; // consume closing '"'
        return v;
    }

    Value ParseNumber() {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p))
                           || *p == '.' || *p == 'e' || *p == 'E'
                           || *p == '+' || *p == '-'))
            ++p;
        char* endPtr = nullptr;
        double d = std::strtod(start, &endPtr);
        // Clamp advance to what strtod actually consumed
        if (endPtr && endPtr > start) p = endPtr;
        return Value::Number(d);
    }
};

static Value Parse(const std::string& json) {
    Parser parser(json);
    return parser.ParseValue();
}

} // namespace Json

// ============================================================================
// Helpers — convert between XMFLOAT3/XMFLOAT4 and Json::Value
// ============================================================================
namespace {

Json::Value Float3ToJson(const XMFLOAT3& v) {
    auto a = Json::Value::Array();
    a.Push(Json::Value::Number(v.x));
    a.Push(Json::Value::Number(v.y));
    a.Push(Json::Value::Number(v.z));
    return a;
}

Json::Value Float4ToJson(const XMFLOAT4& v) {
    auto a = Json::Value::Array();
    a.Push(Json::Value::Number(v.x));
    a.Push(Json::Value::Number(v.y));
    a.Push(Json::Value::Number(v.z));
    a.Push(Json::Value::Number(v.w));
    return a;
}

XMFLOAT3 JsonToFloat3(const Json::Value& v,
                       XMFLOAT3 def = { 0.f, 0.f, 0.f }) {
    if (v.type != Json::Type::Array || v.aval.size() < 3) return def;
    return { v.aval[0].AsFloat(), v.aval[1].AsFloat(), v.aval[2].AsFloat() };
}

XMFLOAT4 JsonToFloat4(const Json::Value& v,
                       XMFLOAT4 def = { 0.f, 0.f, 0.f, 1.f }) {
    if (v.type != Json::Type::Array || v.aval.size() < 4) return def;
    return { v.aval[0].AsFloat(), v.aval[1].AsFloat(),
             v.aval[2].AsFloat(), v.aval[3].AsFloat() };
}

// Ensure a directory exists (creates it if absent, single level only).
void EnsureDir(const std::string& path) {
    CreateDirectoryA(path.c_str(), nullptr); // OK if already exists
}

// Extract the directory portion of a path (up to and including the last separator).
std::string DirOf(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos != std::string::npos) ? path.substr(0, pos) : "";
}

} // anonymous namespace

// ============================================================================
// SceneSerializer::Save
// ============================================================================
bool SceneSerializer::Save(const Scene& scene, const std::string& filepath) {
    using namespace Json;

    // ---- Build JSON tree ----------------------------------------------------
    Value root = Value::Object();
    root.Set("version",  Value::Number(1.0));
    root.Set("scene",    Value::Str(scene.GetName()));

    Value goArray = Value::Array();

    for (auto& goPtr : scene.GetGameObjects()) {
        const GameObject* go = goPtr.get();

        Value goObj = Value::Object();
        goObj.Set("name",   Value::Str(go->GetName()));
        goObj.Set("active", Value::Bool(go->IsActive()));
        goObj.Set("layer",  Value::Number(go->GetLayer()));

        // ---- Transform ------------------------------------------------------
        if (const Transform* t = go->GetTransform()) {
            Value tObj = Value::Object();
            tObj.Set("position", Float3ToJson(t->GetLocalPosition()));
            tObj.Set("rotation", Float4ToJson(t->GetRotationQuat()));
            tObj.Set("scale",    Float3ToJson(t->GetLocalScale()));
            goObj.Set("Transform", tObj);
        }

        // ---- Camera ---------------------------------------------------------
        if (const Camera* cam = go->GetComponent<Camera>()) {
            Value cObj = Value::Object();
            cObj.Set("yaw",       Value::Number(cam->GetYaw()));
            cObj.Set("pitch",     Value::Number(cam->GetPitch()));
            cObj.Set("fov",       Value::Number(cam->GetFOV()));
            cObj.Set("nearZ",     Value::Number(cam->GetNearZ()));
            cObj.Set("farZ",      Value::Number(cam->GetFarZ()));
            cObj.Set("moveSpeed", Value::Number(cam->GetMoveSpeed()));
            cObj.Set("lookSpeed", Value::Number(cam->GetLookSpeed()));
            goObj.Set("Camera", cObj);
        }

        // ---- Rigidbody ------------------------------------------------------
        if (const Rigidbody* rb = go->GetComponent<Rigidbody>()) {
            Value rObj = Value::Object();

            switch (rb->GetShapeType()) {
            case Rigidbody::ShapeType::Box:
                rObj.Set("shape",       Value::Str("Box"));
                rObj.Set("halfExtents", Float3ToJson(rb->GetHalfExtents()));
                break;
            case Rigidbody::ShapeType::Sphere:
                rObj.Set("shape",  Value::Str("Sphere"));
                rObj.Set("radius", Value::Number(rb->GetRadius()));
                break;
            case Rigidbody::ShapeType::Capsule:
                rObj.Set("shape",      Value::Str("Capsule"));
                rObj.Set("radius",     Value::Number(rb->GetRadius()));
                rObj.Set("halfHeight", Value::Number(rb->GetCapsuleHalfHeight()));
                break;
            }

            rObj.Set("mass",        Value::Number(rb->GetMass()));
            rObj.Set("restitution", Value::Number(rb->GetRestitution()));
            rObj.Set("friction",    Value::Number(rb->GetFriction()));
            rObj.Set("isStatic",    Value::Bool(rb->IsStatic()));
            rObj.Set("isKinematic", Value::Bool(rb->IsKinematic()));
            rObj.Set("isTrigger",   Value::Bool(rb->IsTrigger()));
            goObj.Set("Rigidbody", rObj);
        }

        goArray.Push(std::move(goObj));
    }

    root.Set("gameObjects", std::move(goArray));

    // ---- Write to disk ------------------------------------------------------
    const std::string dir = DirOf(filepath);
    if (!dir.empty()) EnsureDir(dir);

    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("SceneSerializer::Save — cannot open '%s' for writing",
                  filepath.c_str());
        return false;
    }

    file << Stringify(root);
    if (!file.good()) {
        LOG_ERROR("SceneSerializer::Save — write error on '%s'",
                  filepath.c_str());
        return false;
    }

    LOG_INFO("SceneSerializer::Save — saved '%s'  (%d objects)",
             filepath.c_str(),
             static_cast<int>(scene.GetGameObjects().size()));
    return true;
}

// ============================================================================
// SceneSerializer::Load
// ============================================================================
bool SceneSerializer::Load(const std::string& filepath, Scene& scene) {
    // ---- Read file ----------------------------------------------------------
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("SceneSerializer::Load — cannot open '%s'", filepath.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string json = ss.str();

    // ---- Parse --------------------------------------------------------------
    const Json::Value root = Json::Parse(json);
    if (root.type != Json::Type::Object) {
        LOG_ERROR("SceneSerializer::Load — JSON root is not an object in '%s'",
                  filepath.c_str());
        return false;
    }

    const Json::Value* goArrayPtr = root.Get("gameObjects");
    if (!goArrayPtr || goArrayPtr->type != Json::Type::Array) {
        LOG_WARN("SceneSerializer::Load — 'gameObjects' array missing in '%s'",
                 filepath.c_str());
        return true; // Not a hard error — scene stays as-is
    }

    int applied = 0;

    for (const Json::Value& goVal : goArrayPtr->aval) {
        if (goVal.type != Json::Type::Object) continue;

        // ---- Find matching GameObject by name (any active/inactive) ---------
        const Json::Value* namePtr = goVal.Get("name");
        if (!namePtr) continue;
        const std::string goName = namePtr->AsString();

        // Search all GOs (not just active) so inactive ones are also patched.
        GameObject* go = nullptr;
        for (auto& ptr : scene.GetGameObjects()) {
            if (ptr->GetName() == goName) { go = ptr.get(); break; }
        }
        if (!go) continue; // name not in scene — skip silently

        // ---- Active / layer -------------------------------------------------
        if (const auto* p = goVal.Get("active"))
            go->SetActive(p->AsBool(true));
        if (const auto* p = goVal.Get("layer"))
            go->SetLayer(p->AsInt(0));

        // ---- Transform ------------------------------------------------------
        if (const Json::Value* tVal = goVal.Get("Transform")) {
            Transform* t = go->GetTransform();
            if (t) {
                if (const auto* p = tVal->Get("position"))
                    t->SetPosition(JsonToFloat3(*p));
                if (const auto* p = tVal->Get("rotation"))
                    t->SetRotationQuat(JsonToFloat4(*p));
                if (const auto* p = tVal->Get("scale"))
                    t->SetScale(JsonToFloat3(*p, { 1.f, 1.f, 1.f }));
            }
        }

        // ---- Camera ---------------------------------------------------------
        if (const Json::Value* cVal = goVal.Get("Camera")) {
            if (Camera* cam = go->GetComponent<Camera>()) {
                if (const auto* p = cVal->Get("yaw"))       cam->SetYaw(p->AsFloat());
                if (const auto* p = cVal->Get("pitch"))     cam->SetPitch(p->AsFloat());
                if (const auto* p = cVal->Get("fov"))       cam->SetFOV(p->AsFloat());
                if (const auto* pn = cVal->Get("nearZ")) {
                    float nearZ = pn->AsFloat(0.1f);
                    float farZ  = 1000.f;
                    if (const auto* pf = cVal->Get("farZ")) farZ = pf->AsFloat(1000.f);
                    cam->SetNearFar(nearZ, farZ);
                }
                if (const auto* p = cVal->Get("moveSpeed")) cam->SetMoveSpeed(p->AsFloat(5.f));
                if (const auto* p = cVal->Get("lookSpeed")) cam->SetLookSpeed(p->AsFloat(0.15f));
            }
        }

        // ---- Rigidbody ------------------------------------------------------
        if (const Json::Value* rVal = goVal.Get("Rigidbody")) {
            if (Rigidbody* rb = go->GetComponent<Rigidbody>()) {
                // Shape — only override if key present (preserves code-set shape)
                if (const auto* sp = rVal->Get("shape")) {
                    const std::string& shape = sp->AsString();
                    if (shape == "Box") {
                        XMFLOAT3 he = { 0.5f, 0.5f, 0.5f };
                        if (const auto* p = rVal->Get("halfExtents"))
                            he = JsonToFloat3(*p, he);
                        rb->SetBoxHalfExtents(he);
                    } else if (shape == "Sphere") {
                        float r = 0.5f;
                        if (const auto* p = rVal->Get("radius")) r = p->AsFloat(r);
                        rb->SetSphereRadius(r);
                    } else if (shape == "Capsule") {
                        float r = 0.3f, hh = 0.7f;
                        if (const auto* p = rVal->Get("radius"))     r  = p->AsFloat(r);
                        if (const auto* p = rVal->Get("halfHeight"))  hh = p->AsFloat(hh);
                        rb->SetCapsuleShape(r, hh);
                    }
                }

                if (const auto* p = rVal->Get("mass"))        rb->SetMass       (p->AsFloat());
                if (const auto* p = rVal->Get("restitution")) rb->SetRestitution(p->AsFloat());
                if (const auto* p = rVal->Get("friction"))    rb->SetFriction   (p->AsFloat());
                if (const auto* p = rVal->Get("isStatic"))    rb->SetStatic     (p->AsBool());
                if (const auto* p = rVal->Get("isKinematic")) rb->SetKinematic  (p->AsBool());
                if (const auto* p = rVal->Get("isTrigger"))   rb->SetTrigger    (p->AsBool());
            }
        }

        ++applied;
    }

    LOG_INFO("SceneSerializer::Load — loaded '%s'  (%d/%d objects patched)",
             filepath.c_str(),
             applied,
             static_cast<int>(goArrayPtr->aval.size()));
    return true;
}

} // namespace VibeEngine
