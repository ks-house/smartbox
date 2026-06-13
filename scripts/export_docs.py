import os
import json
import datetime
import argparse
import re

def get_latest_conversation_dir(base_dir):
    """Finds the most recently modified conversation directory under the brain path."""
    if not os.path.exists(base_dir):
        raise FileNotFoundError(f"Brain directory not found: {base_dir}")
    
    subdirs = [os.path.join(base_dir, d) for d in os.listdir(base_dir)]
    subdirs = [d for d in subdirs if os.path.isdir(d)]
    
    if not subdirs:
        raise ValueError(f"No conversation directories found in {base_dir}")
        
    # Sort by modification time to get the most recent active conversation
    return max(subdirs, key=os.path.getmtime)

def clean_user_input(text):
    """Extracts text within <USER_REQUEST> if it exists, otherwise cleans metadata."""
    if not text:
        return ""
    
    # Try to find <USER_REQUEST> tags
    match = re.search(r'<USER_REQUEST>\s*(.*?)\s*</USER_REQUEST>', text, re.DOTALL)
    if match:
        return match.group(1).strip()
        
    # Fallback: remove metadata section if it exists
    if "<ADDITIONAL_METADATA>" in text:
        text = text.split("<ADDITIONAL_METADATA>")[0].strip()
        
    return text.strip()

def clean_workspace_links(text, workspace_root=None):
    """Replaces absolute workspace paths (both file:/// and raw) with relative paths."""
    if not text:
        return ""
    if workspace_root is None:
        workspace_root = os.getcwd()
    
    ws_path = os.path.abspath(workspace_root)
    parts = re.split(r'[/\\]', ws_path)
    parts_esc = [re.escape(p) for p in parts]
    ws_pattern_str = r'[/\\]'.join(parts_esc)
    
    # Match both file:/// and raw workspace paths
    pattern = re.compile(
        r'(file:///)?(' + ws_pattern_str + r')(?:[/\\]([^\s\)\`\'\"\]]*))?',
        re.IGNORECASE
    )
    
    def replace_match(match):
        subpath = match.group(3) or ""
        subpath_normalized = subpath.replace('\\', '/')
        if subpath_normalized:
            return '../' + subpath_normalized
        else:
            return '..'
            
    return pattern.sub(replace_match, text)



def main():
    parser = argparse.ArgumentParser(description="Export Antigravity IDE Chat transcript and reports.")
    parser.add_argument("title", help="Title for the exported files (e.g., '릴레이연결')")
    parser.add_argument("--num", default="01", help="Sequence number (default: '01')")
    args = parser.parse_args()

    # 1. Date calculation (YYMMDD)
    # Since today is in 2026, it will output "260613"
    today = datetime.datetime.now().strftime("%y%m%d")
    
    os.makedirs("prompts", exist_ok=True)
    os.makedirs("reports", exist_ok=True)

    # Automatically calculate the next sequence number to prevent overwriting!
    prompt_pattern = re.compile(rf"^{today}(\d{{2}})_{re.escape(args.title)}_prompt\.md$")
    existing_nums = []
    for f in os.listdir("prompts"):
        m = prompt_pattern.match(f)
        if m:
            existing_nums.append(int(m.group(1)))

    if args.num:
        num_val = int(args.num)
        while num_val in existing_nums:
            num_val += 1
        num_str = f"{num_val:02d}"
    else:
        if existing_nums:
            next_num = max(existing_nums) + 1
        else:
            next_num = 1
        num_str = f"{next_num:02d}"

    # File naming scheme
    prompt_filename = f"{today}{num_str}_{args.title}_prompt.md"
    report_filename = f"{today}{num_str}_{args.title}_report.md"

    print(f"[*] Export target filenames (Auto-incremented to #{num_str}):")
    print(f"    - Prompt: prompts/{prompt_filename}")
    print(f"    - Report: reports/{report_filename}")

    # 2. Paths configuration
    home_dir = os.path.expanduser("~")
    brain_base_dir = os.path.join(home_dir, ".gemini", "antigravity-ide", "brain")
    
    try:
        active_conv_dir = get_latest_conversation_dir(brain_base_dir)
        conv_id = os.path.basename(active_conv_dir)
        print(f"[*] Found active conversation directory: {conv_id}")
    except Exception as e:
        print(f"[!] Error finding conversation folder: {e}")
        return

    transcript_path = os.path.join(active_conv_dir, ".system_generated", "logs", "transcript.jsonl")
    if not os.path.exists(transcript_path):
        print(f"[!] Transcript log file not found at: {transcript_path}")
        return

    # 3. Read and parse the transcript
    dialogue = []
    print(f"[*] Parsing transcript logs...")
    
    with open(transcript_path, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            try:
                step = json.loads(line)
                step_type = step.get("type")
                source = step.get("source")
                content = step.get("content", "")

                # User Input
                if source == "USER_EXPLICIT" and step_type == "USER_INPUT":
                    cleaned_content = clean_user_input(content)
                    if cleaned_content:
                        cleaned_content = clean_workspace_links(cleaned_content)
                        dialogue.append(("User", cleaned_content))
                
                # Model Response
                elif source == "MODEL" and step_type == "PLANNER_RESPONSE":
                    if content and not content.startswith("I will"):
                        # Skip tool execution intents (e.g., "I will read...", "I will search...")
                        cleaned_content = clean_workspace_links(content)
                        dialogue.append(("AI", cleaned_content))
            except Exception as parse_err:
                # Silently skip malformed lines if any
                continue

    # 4. Generate the Prompt Markdown
    prompt_filepath = os.path.join("prompts", prompt_filename)
    
    markdown_content = [
        f"# 🤖 {prompt_filename}\n",
        f"- **일시:** {datetime.datetime.now().strftime('%Y년 %m월 %d일')}",
        f"- **주제:** {args.title}",
        "\n---\n",
        "## 💬 대화 기록 (Transcript)\n"
    ]

    for role, text in dialogue:
        if role == "User":
            markdown_content.append(f"### 👤 사용자 (User)\n> {text.replace('\n', '\n> ')}\n")
        else:
            markdown_content.append(f"### 🤖 AI (Model)\n> {text.replace('\n', '\n> ')}\n")
        markdown_content.append("---\n")

    with open(prompt_filepath, "w", encoding="utf-8") as f:
        f.write("\n".join(markdown_content))
    print(f"[+] Successfully exported prompt log to: {prompt_filepath}")

    # 5. Move or Recreate Report file if it exists, otherwise output template
    report_filepath = os.path.join("reports", report_filename)
    
    # SAFETY CHECK: If report file already exists, do not overwrite it!
    if os.path.exists(report_filepath):
        print(f"[*] Report file '{report_filepath}' already exists. Skipping report creation to prevent overwriting.")
    else:
        # Search for existing reports for this title (any date, any number)
        report_pattern = re.compile(rf"^\d{{6}}(\d{{2}})_{re.escape(args.title)}_report\.md$")
        existing_reports = []
        for r in os.listdir("reports"):
            m = report_pattern.match(r)
            if m:
                existing_reports.append((int(m.group(1)), r))
        
        if existing_reports:
            # Copy the latest (highest sequence number) report to build upon it
            _, latest_report_file = max(existing_reports, key=lambda x: x[0])
            latest_report_path = os.path.join("reports", latest_report_file)
            with open(latest_report_path, "r", encoding="utf-8") as src_f:
                report_data = src_f.read()
            
            # Replace the title in markdown if it matches the format
            report_data = re.sub(r'# 📋 \d+.*\_report\.md', f"# 📋 {report_filename}", report_data)
            
            with open(report_filepath, "w", encoding="utf-8") as dest_f:
                dest_f.write(report_data)
            print(f"[+] Successfully copied and renamed report template from '{latest_report_file}' to: {report_filepath}")
        else:
            # Generate a default report template if none exists
            report_template = f"""# 📋 {report_filename}
    
- **프로젝트명:** SmartBox
- **작성일자:** {datetime.datetime.now().strftime('%Y년 %m월 %d일')}
- **주제:** {args.title}
    
---
    
## 1. 이슈 개요 (Issue Overview)
(여기에 발생한 하드웨어/소프트웨어 이슈 개요를 작성하세요.)
    
## 2. 원인 분석 (Root Cause Analysis)
(이슈의 기술적 원인을 전기적/논리적 분석을 바탕으로 작성하세요.)
    
## 3. 해결 설계 및 구현 (Solution Design & Implementation)
(해결 방법 및 수정된 코드 조각을 설명하세요.)
    
## 4. 검증 결과 (Verification Results)
(이슈가 어떻게 테스트되고 검증되었는지 기록하세요.)
"""
            with open(report_filepath, "w", encoding="utf-8") as f:
                f.write(report_template)
            print(f"[+] Created report template at: {report_filepath}")

if __name__ == "__main__":
    main()
