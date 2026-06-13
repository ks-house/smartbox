import os
import glob
import re

def clean_file_links(filepath, workspace_root):
    ws_path_normalized = workspace_root.replace('\\', '/').rstrip('/')
    
    # Pattern to match: file:///<workspace_root>/<any_path_until_space_or_closing_paren>
    pattern = re.compile(r'file:///(' + re.escape(ws_path_normalized) + r'/[^\s\)]+)', re.IGNORECASE)
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    modified = False
    matches = pattern.findall(content)
    
    if matches:
        # Sort matches by length descending to replace longer paths first
        for match_path in sorted(set(matches), key=len, reverse=True):
            target_abs = os.path.abspath(match_path)
            start_dir = os.path.abspath(os.path.dirname(filepath))
            rel_path = os.path.relpath(target_abs, start_dir).replace('\\', '/')
            
            full_match = 'file:///' + match_path
            content = content.replace(full_match, rel_path)
            modified = True
            
    if modified:
        print(f"[+] Cleaned absolute links in: {filepath}")
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

def main():
    workspace_root = os.path.abspath(os.getcwd())
    print(f"[*] Scanning markdown files in workspace: {workspace_root}")
    
    for root, dirs, files in os.walk('.'):
        # Exclude common temporary or config directories
        if any(p in root for p in ['.git', '.pio', '.cache', '.vscode', 'node_modules']):
            continue
        for file in files:
            if file.endswith('.md'):
                clean_file_links(os.path.join(root, file), workspace_root)

if __name__ == "__main__":
    main()
