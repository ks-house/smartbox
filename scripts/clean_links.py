import os
import re

def clean_file_links(filepath, workspace_root):
    ws_path = os.path.abspath(workspace_root)
    parts = re.split(r'[/\\]', ws_path)
    parts_esc = [re.escape(p) for p in parts]
    ws_pattern_str = r'[/\\]'.join(parts_esc)
    
    # Pattern to match:
    # Group 1: optional file:/// prefix
    # Group 2: the workspace root path
    # Group 3: optional subpath (any characters except spaces, parentheses, quotes, ticks, brackets)
    pattern = re.compile(
        r'(file:///)?(' + ws_pattern_str + r')(?:[/\\]([^\s\)\`\'\"\]]*))?',
        re.IGNORECASE
    )
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    modified = False
    matches = list(pattern.finditer(content))
    
    if matches:
        # Sort matched texts by length descending to replace longer paths first
        matched_texts = sorted(list(set(m.group(0) for m in matches)), key=len, reverse=True)
        for full_match in matched_texts:
            if len(full_match.strip()) <= 4:
                continue
                
            m = pattern.match(full_match)
            if not m:
                continue
            
            subpath = m.group(3) or ""
            subpath_normalized = subpath.replace('/', os.sep).replace('\\', os.sep)
            target_abs = os.path.abspath(os.path.join(ws_path, subpath_normalized))
            
            start_dir = os.path.abspath(os.path.dirname(filepath))
            rel_path = os.path.relpath(target_abs, start_dir).replace('\\', '/')
            
            content = content.replace(full_match, rel_path)
            modified = True
            
    if modified:
        print(f"[+] Cleaned absolute paths/links in: {filepath}")
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

def main():
    workspace_root = os.path.abspath(os.getcwd())
    print(f"[*] Scanning workspace for absolute paths: {workspace_root}")
    
    for root, dirs, files in os.walk('.'):
        # Exclude common temporary, config, and scripts directories to prevent self-modification
        if any(p in root for p in ['.git', '.pio', '.cache', '.vscode', 'node_modules', 'scripts']):
            continue
        for file in files:
            if file.endswith(('.md', '.cpp', '.h')):
                clean_file_links(os.path.join(root, file), workspace_root)

if __name__ == "__main__":
    main()
