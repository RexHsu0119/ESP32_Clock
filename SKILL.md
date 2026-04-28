# Skill: Commit Code with Changelog

## Purpose
此技能用於在開發流程中快速執行 Git commit 與 push 動作，並自動產生 changelog.txt，記錄從上次 commit 到目前的修改摘要與日期。

## Usage
- 觸發詞：`幫我commit code`
- 行為：
  1. 收集從上次 commit 到現在的修改紀錄，以及 AI 在此期間的協助內容。
  2. 將紀錄附上日期 (格式：YYYY/MM/DD)，寫入 `changelog.txt`。
  3. 產生 commit 訊息，包含整理後的修改重點摘要。
  4. 在 commit 前，列出以下資訊讓使用者確認：
     - 日期
     - 修改檔案清單
     - changelog.txt 新增內容
     - commit 訊息摘要
  5. 使用者輸入 `Yes` → 先執行 commit，再 push 到目前分支；輸入 `No` → 取消。

## Example
```bash
# 使用者輸入：
幫我commit code

# 系統顯示：
日期：2026/04/28
修改檔案：
- src/main.c
- vibration_skill.c

Changelog 新增：
[2026/04/28]
- 新增 Commit Code with Changelog 技能文件 SKILL.md
- 修正 SKILL.md 範例中觸發詞大小寫不一致問題（Code → code）
- AI 協助整理本次變更摘要與 commit 訊息草稿

Commit 訊息：
"Add commit workflow skill and changelog"

是否要執行 commit？ (Yes/No)

# 使用者輸入：
Yes

# 系統執行：
git add .
git commit -m "Add commit workflow skill and changelog"
git push
