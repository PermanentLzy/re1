#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""提取PDF文本用于查看性能评分标准"""
import fitz  # pymupdf
import sys, io

# 强制 UTF-8 输出
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

pdf_path = r"d:\Github\MyCompiler_re\MyCompiler-main\编译系统实践.pdf"
out_path = r"d:\Github\MyCompiler_re\MyCompiler-main\pdf_extract.txt"
doc = fitz.open(pdf_path)

with open(out_path, "w", encoding="utf-8") as f:
    f.write(f"PDF共 {len(doc)} 页\n")
    f.write("=" * 80 + "\n")

    # 全文输出，每页都写
    for page_num in range(len(doc)):
        page = doc[page_num]
        text = page.get_text()
        f.write(f"\n----- 第 {page_num + 1} 页 -----\n")
        f.write(text)
        f.write("\n" + "-" * 80 + "\n")

print(f"Saved to {out_path}")
print(f"Pages: {len(doc)}")
