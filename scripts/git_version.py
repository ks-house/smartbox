import subprocess
import os

Import("env")

def get_git_version():
    base_version = "0.1.0"
    try:
        # Try to read base version from src/SmartBoxController.h
        proj_dir = env.get("PROJECT_DIR", "")
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

# Add the macro definition
env.Append(CPPDEFINES=[
    ("FIRMWARE_VERSION_OVERRIDE", f'\\"{version}\\"')
])
