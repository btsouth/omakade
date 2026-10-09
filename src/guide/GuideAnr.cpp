#include "guide/GuideAnr.h"
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>

namespace {
bool execute(const QString& script, const QProcessEnvironment& environment) {
  if (environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() ||
      environment.value("HYPRLAND_INSTANCE_SIGNATURE") == "omabox-guard") return true;
  QProcess process;
  process.setProcessEnvironment(environment);
  process.start("hyprctl", {"eval", script});
  if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(); return false; }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
         process.readAllStandardOutput().trimmed() == "ok";
}
bool valid(const QString& token) {
  return QRegularExpression("^[a-f0-9]{32}$").match(token).hasMatch();
}
QString prune() {
  // This runs on Hyprland's serialized IPC thread, including the liveness
  // check. Start time protects against PID reuse; zombies no longer own leases.
  return QStringLiteral(R"lua(
local function owner_alive(owner)
  if type(owner)~="table" then return false end
  local f=io.open("/proc/"..owner.pid.."/stat", "r")
  if not f then return false end
  local raw=f:read("*l"); f:close()
  local tail=raw and raw:match(".*%) (.*)")
  if not tail then return false end
  local fields={}; for field in tail:gmatch("%S+") do fields[#fields+1]=field end
  return fields[1]~="Z" and fields[1]~="X" and fields[20]==owner.start
end
local function restore(s)
  if next(s.tokens)~=nil then return end
  if hl.config==s.wrapper then hl.config=s.config end
  if not s.changed and hl.get_config("misc.enable_anr_dialog")==false then
    s.config({misc={enable_anr_dialog=s.saved}})
  end
  omakade_anr_leases=nil
end
local s=omakade_anr_leases
if s then
  for token,owner in pairs(s.tokens) do
    if not owner_alive(owner) then s.tokens[token]=nil end
  end
  restore(s)
end
)lua");
}
}

bool GuideAnr::acquire(const QString& token, const QProcessEnvironment& environment) {
  if (!valid(token)) return false;
  const auto pid = QCoreApplication::applicationPid();
  QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!stat.open(QIODevice::ReadOnly)) return false;
  const auto raw = stat.readAll();
  const auto fields = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ');
  if (fields.size() < 20 || fields[19].toLongLong() <= 0) return false;
  // Lua IPC is serialized on the compositor thread. Observe explicit config
  // writes, including false -> false, which a value comparison cannot detect.
  // Restore the original function without replacing a user's later wrapper.
  return execute(prune() + QStringLiteral(R"lua(
if not omakade_anr_leases then
  local saved = hl.get_config("misc.enable_anr_dialog")
  assert(type(saved) == "boolean", "ANR option unavailable")
  local s = {tokens={}, saved=saved, config=hl.config, changed=false}
  s.wrapper = function(c)
    if c.misc and c.misc.enable_anr_dialog ~= nil then s.changed=true end
    return s.config(c)
  end
  omakade_anr_leases=s
  hl.config=s.wrapper
  s.config({misc={enable_anr_dialog=false}})
end
omakade_anr_leases.tokens["%1"]={pid="%2",start="%3"}
)lua").arg(token).arg(pid).arg(QString::fromLatin1(fields[19])), environment);
}

void GuideAnr::release(const QString& token, const QProcessEnvironment& environment) {
  if (!valid(token)) return;
  execute(prune() + QStringLiteral(R"lua(
local s=omakade_anr_leases
if s and s.tokens["%1"] then
  s.tokens["%1"]=nil
  restore(s)
end
)lua").arg(token), environment);
}
