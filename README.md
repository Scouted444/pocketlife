# PocketLife

A small Sims-style life sim for the **Nintendo DS**.

- **Top screen:** your lot in real 3D. Press **X** to switch between a straight **top-down** view and a **2.5D isometric** view. The camera animates smoothly between the two, and **L/R** rotate it in 90° steps.
- **Bottom screen:** a touch UI with needs bars, action menus, skills, **Buy mode** (a catalog with real icons) and **Build mode** (walls, windows, doors, floors).
- **Assets:** all the furniture, walls, doors and windows are low-poly models from **[Kenney's Furniture Kit](https://kenney.nl/assets/furniture-kit)** (CC0). The build converts them to DS display lists automatically. The buy-menu icons come from the kit's own renders.

## Getting the ROM

Every push to `main` builds the ROM on GitHub Actions. Go to **Releases → latest → `pocketlife.nds`**, then copy it to your flashcart / SD card or open it in melonDS / DeSmuME.

Saving uses the SD card (`/pocketlife.sav`). It works on flashcarts and in melonDS with an SD image / DLDI set up.

## Controls

| | Live mode | Buy mode | Build mode |
|---|---|---|---|
| D-pad | move the cursor (screen-relative) | move the item | move the cursor (hold A to drag walls) |
| A | open the action menu (object / sim / "Go Here") | place / pick up to move | build with the selected tool |
| B | cancel the current action | put the item back | erase the wall on the selected edge |
| Y | switch Sim | rotate the item / sell the item under the cursor | toggle North / West edge |
| X | top-down ↔ 2.5D | same | same |
| L / R | rotate the camera | same | same |
| SELECT | zoom | same | same |
| START | pause | | |

Touch: speed buttons (top), LIVE / BUY / BUILD / OPTIONS tabs (bottom), menus, catalog, tools.

## Gameplay

- Two housemates made in **Create-A-Sim** (name, body, skin, hair, shirt, pants).
- Six needs: Hunger, Energy, Bladder, Hygiene, Fun, Social. Mood colours the plumbob.
- **Free will:** idle Sims look after themselves, sleep at night, and chat with each other.
- Skills (Cooking, Logic, Charisma) raise your work-from-home pay. Logic and Charisma both count.
- Socials (Chat, Joke, Hug) and a relationship meter.
- Money, bills every 3 days, groceries for cooking, buy/sell/move furniture.
- When everyone is asleep, time fast-forwards.
- Walls: Up / Cutaway / Down (Options). Cutaway lowers the walls between the camera and the cursor in 2.5D.

## How the build works

1. `tools/fetch_assets.sh` downloads the Kenney Furniture Kit into `assets/kit`. To avoid downloading at build time, unzip the kit there yourself and commit it.
2. `tools/convert.py` picks the models the game needs and does the following:
   - normalises them so 1 wall = 1 tile
   - auto-decimates each model to a polygon cap (the DS can draw ~2048 polygons per frame)
   - writes packed GX display lists with per-material colours and flat normals for hardware lighting
   - makes 32×32 icons

   It writes `source/gen_assets.c` and `include/gen_assets.h`, and prints a table showing where each model came from. If a model is missing, a coloured box stands in so the build never breaks.
3. `tools/build.sh` builds with devkitARM. If the project Makefile doesn't match the installed libnds, it falls back to the official template.

At startup the game checks which polygon winding the DS treats as front-facing (it reads the hardware polygon counter), so back-face culling is right whatever the model export did.

## Credits

- 3D furniture: **Kenney** (www.kenney.nl), CC0.
- Built with devkitPro / libnds.
