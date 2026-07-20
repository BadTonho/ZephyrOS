import os
import re

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    out_lines = []
    for i, line in enumerate(lines):
        # Check inline if
        m_if = re.search(r'^(\s*)if\s*\((.*?)\)\s*kfree\((.*?)\);', line)
        if m_if:
            indent = m_if.group(1)
            cond = m_if.group(2)
            ptr = m_if.group(3)
            # Check if next line already nulls it
            if i + 1 < len(lines) and (f"{ptr} = NULL;" in lines[i+1] or f"{ptr} = 0;" in lines[i+1]):
                out_lines.append(line)
            else:
                out_lines.append(f"{indent}if ({cond}) {{ kfree({ptr}); {ptr} = 0; }}\n")
            continue
        
        # Check normal kfree
        m_kfree = re.search(r'^(\s*)kfree\((.*?)\);', line)
        if m_kfree:
            indent = m_kfree.group(1)
            ptr = m_kfree.group(2)
            # Check if next line already nulls it
            if i + 1 < len(lines) and (f"{ptr} = NULL;" in lines[i+1] or f"{ptr} = 0;" in lines[i+1]):
                out_lines.append(line)
            else:
                # If pointer is a cast like kfree((void*)proc->kernel_stack); we need to extract proc->kernel_stack
                # Actually, better to just take the whole thing and regex out the cast for the assignment
                clean_ptr = re.sub(r'^\(.*\)', '', ptr).strip()
                out_lines.append(line)
                out_lines.append(f"{indent}{clean_ptr} = 0;\n")
            continue
        
        out_lines.append(line)

    with open(filepath, 'w', encoding='utf-8') as f:
        f.writelines(out_lines)

src_dir = r"c:\Users\Admin\Desktop\ProjetosCode\Sistema\src"
for root, dirs, files in os.walk(src_dir):
    for file in files:
        if file.endswith(".c"):
            process_file(os.path.join(root, file))
