import subprocess
import os

Import("env") # type: ignore

def get_git_version():
    base_version = "0.1.0"
    try:
        # Try to read base version from src/SmartBoxController.h
        proj_dir = env.get("PROJECT_DIR", "") # type: ignore
        header_path = os.path.join(proj_dir, "src", "SmartBoxController.h")
        if os.path.exists(header_path):
            with open(header_path, "r", encoding="utf-8") as f:
                for line in f:
                    if "static constexpr const char *FIRMWARE_VERSION =" in line:
                        parts = line.split('"')
                        if len(parts) >= 2:
                            base_version = parts[1]
                            break
    except Exception as e:
        print(f"[git_version] Warning: Failed to read SmartBoxController.h: {e}")

    try:
        # Get short git hash
        commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('utf-8').strip()
        # Check if working directory is dirty
        status = subprocess.check_output(['git', 'status', '--porcelain']).decode('utf-8').strip()
        if status:
            return f"{base_version}-g{commit}-dirty"
        else:
            return f"{base_version}-g{commit}"
    except Exception as e:
        print(f"[git_version] Warning: git rev-parse failed, using fallback: {e}")
        return f"{base_version}-unknown"

version = get_git_version()
print(f"[git_version] Extracted dynamic version: {version}")

# Generate a header file to avoid global CPPDEFINES changes (which triggers full rebuilds)
proj_dir = env.get("PROJECT_DIR", "") # type: ignore
out_file = os.path.join(proj_dir, "src", "git_version.h")
new_content = f'#define FIRMWARE_VERSION_OVERRIDE "{version}"\n'

# Only write if the content actually changed to prevent unnecessary recompilations
write_file = True
if os.path.exists(out_file):
    with open(out_file, "r") as f:
        if f.read() == new_content:
            write_file = False

if write_file:
    with open(out_file, "w") as f:
        f.write(new_content)
    print(f"[git_version] Updated src/git_version.h")
