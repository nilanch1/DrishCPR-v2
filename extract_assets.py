import os
import re
from collections import Counter

# Paths
ROOT_DIR = os.path.dirname(__file__)
DATA_DIR = os.path.join(ROOT_DIR, "data")
CSS_DIR = os.path.join(DATA_DIR, "css")
JS_DIR = os.path.join(DATA_DIR, "js")

os.makedirs(CSS_DIR, exist_ok=True)
os.makedirs(JS_DIR, exist_ok=True)

# Regex
style_pattern = re.compile(r"<style[^>]*>(.*?)</style>", re.DOTALL | re.IGNORECASE)
script_pattern = re.compile(r"<script([^>]*)>(.*?)</script>", re.DOTALL | re.IGNORECASE)

# Collect all extracted CSS/JS first
all_css = {}
all_js = {}

for filename in os.listdir(DATA_DIR):
    if filename.endswith(".html"):
        filepath = os.path.join(DATA_DIR, filename)
        with open(filepath, "r", encoding="utf-8") as f:
            html = f.read()

        base = os.path.splitext(filename)[0]

        css_blocks = [m.group(1).strip() for m in style_pattern.finditer(html) if m.group(1).strip()]
        js_blocks = [m.group(2).strip() for m in script_pattern.finditer(html) 
                     if "src=" not in m.group(1) and m.group(2).strip()]

        all_css[base] = css_blocks
        all_js[base] = js_blocks

# Flatten + count occurrences
css_counter = Counter(block for blocks in all_css.values() for block in blocks)
js_counter = Counter(block for blocks in all_js.values() for block in blocks)

# Mark "common" if block appears in >1 file
common_css = [block for block, count in css_counter.items() if count > 1]
common_js = [block for block, count in js_counter.items() if count > 1]

# Write common files
if common_css:
    with open(os.path.join(CSS_DIR, "common.css"), "w", encoding="utf-8") as f:
        f.write("\n\n".join(common_css))
    print(f"[+] Wrote common CSS → css/common.css")

if common_js:
    with open(os.path.join(JS_DIR, "common.js"), "w", encoding="utf-8") as f:
        f.write("\n\n".join(common_js))
    print(f"[+] Wrote common JS → js/common.js")

# Now rewrite HTMLs with per-file + common includes
for filename in os.listdir(DATA_DIR):
    if filename.endswith(".html"):
        filepath = os.path.join(DATA_DIR, filename)
        base = os.path.splitext(filename)[0]

        with open(filepath, "r", encoding="utf-8") as f:
            html = f.read()

        # Remove inline style/script
        html = style_pattern.sub("", html)
        html = script_pattern.sub(lambda m: m.group(0) if "src=" in m.group(1) else "", html)

        # Rebuild <head> with links
        head_inserts = []
        if common_css:
            head_inserts.append('<link rel="stylesheet" href="css/common.css">')
        if all_css[base]:
            css_path = os.path.join(CSS_DIR, f"{base}.css")
            with open(css_path, "w", encoding="utf-8") as f:
                f.write("\n\n".join(block for block in all_css[base] if block not in common_css))
            head_inserts.append(f'<link rel="stylesheet" href="css/{base}.css">')
            print(f"[+] Wrote {base}.css")

        if common_js:
            head_inserts.append('<script src="js/common.js"></script>')
        if all_js[base]:
            js_path = os.path.join(JS_DIR, f"{base}.js")
            with open(js_path, "w", encoding="utf-8") as f:
                f.write("\n\n".join(block for block in all_js[base] if block not in common_js))
            head_inserts.append(f'<script src="js/{base}.js"></script>')
            print(f"[+] Wrote {base}.js")

        # Insert before </head> or </body>
        if "</head>" in html:
            html = html.replace("</head>", "\n".join(head_inserts) + "\n</head>")
        else:
            html = html.replace("</body>", "\n".join(head_inserts) + "\n</body>")

        with open(filepath, "w", encoding="utf-8") as f:
            f.write(html)

        print(f"[✓] Updated {filename}")
