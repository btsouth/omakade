#include "guide/GuideAnr.h"
#include <QProcess>
#include <QRegularExpression>

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
}

bool GuideAnr::acquire(const QString& token, const QProcessEnvironment& environment) {
  if (!valid(token)) return false;
  // Lua IPC is serialized on the compositor thread. Observe explicit config
  // writes, including false -> false, which a value comparison cannot detect.
  // Restore the original function without replacing a user's later wrapper.
  return execute(QStringLiteral(R"lua(
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
omakade_anr_leases.tokens["%1"]=true
)lua").arg(token), environment);
}

void GuideAnr::release(const QString& token, const QProcessEnvironment& environment) {
  if (!valid(token)) return;
  execute(QStringLiteral(R"lua(
local s=omakade_anr_leases
if s and s.tokens["%1"] then
  s.tokens["%1"]=nil
  if next(s.tokens)==nil then
    if hl.config==s.wrapper then hl.config=s.config end
    if not s.changed and hl.get_config("misc.enable_anr_dialog")==false then
      s.config({misc={enable_anr_dialog=s.saved}})
    end
    omakade_anr_leases=nil
  end
end
)lua").arg(token), environment);
}
