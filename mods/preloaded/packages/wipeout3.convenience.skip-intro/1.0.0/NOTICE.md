# Always Skip Intro provenance

This package contains project-authored integration code only. The five hook
locations were recovered from the authenticated stock European Special Edition
executable (`SCES-02845`, SHA-256
`0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`).

It preserves the five stock calls that scan the intro files and fill their LBA
tables, then bypasses only the five direct calls to the title's intro-player
routine. It contains no Sony/Psygnosis media, no Hueponik patch bytes, and no
executable or high-RAM replacement engine.
