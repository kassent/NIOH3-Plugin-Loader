import json
import subprocess
from pathlib import Path

root_dir = Path(__file__).resolve().parent
build_dir = root_dir / "build"

def _find_project_include_dir() -> Path:
    """
    优先使用 <root>/common/include（如果存在），否则回退到 <root>/include。
    """
    common_inc = root_dir / "common" / "include"
    if common_inc.is_dir():
        return common_inc
    return root_dir / "include"

project_include_dir = _find_project_include_dir()
vcpkg_include_dir = build_dir / "vcpkg_installed" / "x64-windows-static" / "include"

# 1) 生成 compile_commands.json
sln_path = build_dir / "HIDWrapper.sln"
subprocess.run(
    ["vs_export.exe", "-c", "Release|x64", "-s", str(sln_path)],
    cwd=str(root_dir),
    check=True,
)

# 2) 修正命令：clang-cl -> clangd，并把 include 路径改为脚本所在目录下的相应路径
compile_commands_file = root_dir / "compile_commands.json"
data = json.loads(compile_commands_file.read_text(encoding="utf-8"))

proj_inc_flag = f"-I{project_include_dir}"
vcpkg_inc_flag = f"-I{vcpkg_include_dir}"

def _fix_command(cmd: str) -> str:
    cmd = cmd.replace("clang-cl.exe", "clangd.exe -std=c++23")

    # 把旧的硬编码 common/include 替换为当前项目目录下的 include
    # 注意：compile_commands.json 里通常会以反斜杠路径出现；这里做子串匹配即可。
    if "\\common\\include" in cmd or "/common/include" in cmd:
        # 尝试替换整个 -Ixxx\common\include 片段（避免误改其它 -I）
        for sep in ("\\common\\include", "/common/include"):
            idx = cmd.find(sep)
            if idx != -1:
                # 从 idx 往前找到该 -I 的起点
                start = cmd.rfind("-I", 0, idx)
                if start != -1:
                    end = idx + len(sep)
                    cmd = cmd[:start] + proj_inc_flag + cmd[end:]
                    break

    # 确保 vcpkg include 存在（避免重复添加）
    if vcpkg_inc_flag not in cmd and vcpkg_include_dir.is_dir():
        if proj_inc_flag in cmd:
            cmd = cmd.replace(proj_inc_flag, f"{proj_inc_flag} {vcpkg_inc_flag}", 1)
        else:
            cmd = f"{cmd} {vcpkg_inc_flag}"

    return cmd

for entry in data:
    if "command" in entry and isinstance(entry["command"], str):
        entry["command"] = _fix_command(entry["command"])
    elif "arguments" in entry and isinstance(entry["arguments"], list):
        # 兼容 arguments 形式（尽量保持原结构）
        args = [("clangd.exe -std=c++23" if a == "clang-cl.exe" else a) for a in entry["arguments"]]
        if proj_inc_flag not in args:
            # 如果原本含有 common/include，则替换为当前项目 include
            for i, a in enumerate(args):
                if isinstance(a, str) and (a.endswith("\\common\\include") or a.endswith("/common/include")) and a.startswith("-I"):
                    args[i] = proj_inc_flag
                    break
        if vcpkg_inc_flag not in args and vcpkg_include_dir.is_dir():
            args.append(vcpkg_inc_flag)
        entry["arguments"] = args

compile_commands_file.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
