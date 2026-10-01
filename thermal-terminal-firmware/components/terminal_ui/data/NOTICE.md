The generated `stroke_data.hpp` and `unifont_data.hpp` contain:

- Stroke codes derived from the Rime stroke.dict.yaml dictionary. The source identifies its main code table as derived from Taiwan's CNS11643 character database and credits the Rime contributors. Rime source and binary data are distributed under the BSD 3-Clause license; see the librime license.
- 16x16 glyphs derived from GNU Unifont BDF. Unifont is distributed under GPLv2-or-later and SIL Open Font License terms with the font embedding exception. See `/usr/share/licenses/bdf-unifont/LICENSE` in the source environment and the Unifont project license for the applicable notices.

The stroke table covers all 6,763 characters in the project's GB2312 frequency file, including alternate stroke codes for characters with multiple accepted codes. Candidate ordering uses the numeric frequency column in `gb2312_by_freq.txt`; exact stroke-code matches are presented before prefix matches. Glyphs include the complete GB2312 set and the Chinese UI/full-width symbol characters.

Regenerate both generated headers from the firmware directory with:

```sh
python3 components/terminal_ui/tools/generate_stroke_data.py \
  --stroke /usr/share/rime-data/stroke.dict.yaml \
  --frequency gb2312_by_freq.txt \
  --bdf /usr/share/fonts/misc/unifont.bdf \
  --output components/terminal_ui/include/terminal_ui/stroke_data.hpp \
  --glyph-output components/oled_display/include/oled_display/unifont_data.hpp
```
