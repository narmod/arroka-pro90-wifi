# 🏊 Arroka Pro 90 — Contrôle WiFi via ESP32 + RS485

Ajout du contrôle WiFi à une pompe à chaleur piscine **Arroka Pro 90** (ByPiscine, 2022) via un ESP32 connecté au bus RS485 de la carte mère, intégré dans **Home Assistant** via **ESPHome**.

> Testé sur carte mère ref. `1.35.1010095` datée 2022-03-23.  
> Probablement compatible avec d'autres PAC de la gamme ByPiscine / Pool Comfort pré-2023.

---

## 📋 Sommaire

- [Matériel nécessaire](#matériel-nécessaire)
- [Câblage](#câblage)
- [Protocole RS485 décodé](#protocole-rs485-décodé)
- [Installation ESPHome](#installation-esphome)
- [Résultat dans Home Assistant](#résultat-dans-home-assistant)
- [Sketch Arduino de diagnostic](#sketch-arduino-de-diagnostic)
- [FAQ](#faq)

---

## 🛒 Matériel nécessaire

| Composant | Référence | Prix |
|-----------|-----------|------|
| Microcontrôleur | ESP32-WROOM-32 (AZ-Delivery DevKit v4) | ~8€ |
| Convertisseur RS485 | Module MAX485 | ~1€ |
| Alimentation | Chargeur USB 5V/1A | ~5€ |
| Câble | Micro-USB data+charge | ~3€ |
| Fils | Dupont M/F | ~2€ |

**Total : ~19€**

---

## 🔌 Câblage

Le connecteur **CN8** de la carte mère expose le bus RS485 :

```
PAC CN8          MAX485              ESP32 AZ-Delivery
──────────────────────────────────────────────────────
A           →    A
B           →    B
GND         →    GND  ←──────────   GND
+12V             ⚠️ NE PAS BRANCHER

                 VCC  ←──────────   U5  (5V, en haut à droite)
                 GND  ←──────────   GND
                 DI   ←──────────   GPIO17 (TX2)
                 RO   ────────────►  GPIO16 (RX2)
                 DE+RE ←─────────   GPIO4
```

> ⚠️ **IMPORTANT** : Ne jamais brancher le +12V de la PAC sur le MAX485.  
> Le module MAX485 est alimenté en **5V** depuis la broche `U5` de l'ESP32.

---

## 📡 Protocole RS485 décodé

### Paramètres

| Paramètre | Valeur |
|-----------|--------|
| Baudrate | **9600 bps** |
| Format | 8N1 |
| Longueur trame | **13 octets** |

### Structure d'une trame

```
[00] [01] [02] [03] [04] [05] [06] [07] [08] [09] [10] [11] [12]
 ID  Teau Cons  ?    ?    ?    ?   FLAG  Teau  ?    ?   0x7F CRC
```

| Byte | Trame | Valeur | Signification |
|------|-------|--------|---------------|
| [00] | toutes | `CC/CD/DD/D0/D2` | ID trame |
| [01] | CC/CD | ex: `0x19` | Temp eau (octet interne, ne pas utiliser pour affichage) |
| [01] | DD | ex: `0x11` = 17 | **Température eau mesurée** (°C) |
| [02] | CC/CD | ex: `0x1C` = 28 | **Consigne température** (°C) |
| [05] | DD | ex: `0x0F` = 15 | **Température air extérieur** (°C) |
| [07] | CC/CD | voir tableau | **FLAG : ON/OFF + MODE** |
| [08] | DD | `0x00`=arrêt / autre=marche | **État compresseur** |
| [11] | toutes | `0x7F` | Marqueur fin de trame (fixe) |
| [12] | toutes | calculé | **CRC** |

### Tableau byte[07] — FLAG

```
0x2C = 0010 1100  →  OFF + HEAT  (arrêt, mode chauffage)
0x6C = 0110 1100  →  ON  + HEAT  (marche, mode chauffage)
0x0C = 0000 1100  →  OFF + COOL  (arrêt, mode refroidissement)
0x4C = 0100 1100  →  ON  + COOL  (marche, mode refroidissement)

Bit 6 (0x40) : ON=1 / OFF=0
Bit 5 (0x20) : HEAT=1 / COOL=0
Bits 3+2 (0x0C) : fixes
```

### Calcul CRC

```cpp
uint8_t x = 0;
for (int i = 0; i < 12; i++) x ^= frame[i];
frame[12] = x ^ 0xBD;  // pour trame CD (commande)
```

### Cycle de trames (normal, ~160ms/cycle)

```
DD → CC → D2 → CC → D0 → (répéter)
```

### Envoi d'une commande

1. Attendre la réception d'une trame `CC`
2. Copier la trame `CC` dans un buffer
3. Modifier `frame[0]=0xCD`, `frame[2]=consigne`, `frame[7]=flag`
4. Recalculer le CRC
5. Passer DE/RE HIGH, envoyer, repasser LOW

---

## ⚙️ Installation ESPHome

### Prérequis
- Home Assistant avec l'addon ESPHome installé
- Accès à l'éditeur de fichiers HA (addon File Editor)

### Structure des fichiers

```
/config/esphome/
├── arroka-pac.yaml
└── components/
    └── arroka/
        ├── __init__.py        ← fichier vide obligatoire
        ├── climate.py         ← composant Python ESPHome
        └── arroka_climate.h   ← code C++ de contrôle
```

### Étape 1 — Copier les fichiers

Via l'addon **File Editor** de Home Assistant, créez les fichiers suivants depuis le dossier [`esphome/`](esphome/) de ce dépôt.

### Étape 2 — Configurer le WiFi

Dans `arroka-pac.yaml`, remplacez :
```yaml
wifi:
  ssid: "VotreSSID"
  password: "VotreMotDePasse"
```

### Étape 3 — Compiler et flasher

1. Dans le dashboard ESPHome, cliquez **"Install"**
2. Choisissez **"Manual download"** → **"Factory format"**
3. Flashez via https://web.esphome.io (branchez l'ESP32 en USB)

### Étape 4 — Intégrer dans Home Assistant

L'appareil sera découvert automatiquement. Ajoutez-le via **Paramètres → Appareils et services → ESPHome**.

---

## 🏠 Résultat dans Home Assistant

Une entité **Climate** apparaît avec :

```
┌─────────────────────────────────────┐
│  🌊 PAC Piscine                      │
│                                      │
│  Actuel : 17°C  ──►  Cible : 28°C  │
│                                      │
│  ❄️ COOL    🔥 HEAT    ⏸ OFF        │
│                                      │
│  Action : CHAUFFAGE EN COURS         │
└─────────────────────────────────────┘
```

---

## 🔧 Sketch Arduino de diagnostic

Le dossier [`arduino/arroka_debug/`](arduino/arroka_debug/) contient un sketch Arduino standalone permettant de :
- Sniffer le bus RS485 sans ESPHome
- Décoder les trames en temps réel
- Envoyer des commandes manuelles via le moniteur série

### Commandes disponibles

| Commande | Action |
|----------|--------|
| `ON` | Allume la PAC |
| `OFF` | Éteint la PAC |
| `HEAT` | Passe en mode chauffage |
| `COOL` | Passe en mode refroidissement |
| `SET 28` | Change la consigne à 28°C |
| `STATUS` | Affiche l'état actuel |

---

## ❓ FAQ

**Q : Compatible avec d'autres modèles ?**  
R : Potentiellement compatible avec toutes les PAC ByPiscine / Pool Comfort commercialisées avant 2023 (avant l'intégration WiFi native). Les modèles post-2023 ont le WiFi intégré.

**Q : Risque de casser la PAC ?**  
R : L'ESP32 est en **écoute passive** sur le bus. Il n'injecte des trames que ponctuellement pour les commandes, en copiant exactement le format du boîtier physique. Le boîtier physique continue de fonctionner normalement.

**Q : Faut-il couper l'alimentation pour installer ?**  
R : Oui, coupez l'alimentation de la PAC avant de brancher les fils sur CN8.

**Q : Le boîtier physique continue de fonctionner ?**  
R : Oui, les deux contrôles (ESP32 et boîtier) coexistent. Le dernier à envoyer une commande gagne.

---

## 📜 Licence

MIT License — libre d'utilisation, modification et redistribution.

---

## 🤝 Contribution

Les PR sont les bienvenues ! En particulier :
- Tests sur d'autres modèles de PAC ByPiscine
- Intégration avec d'autres systèmes domotique (Jeedom, Domoticz...)
- Amélioration de la documentation

---

*Projet réalisé par reverse engineering du protocole RS485 propriétaire.*  
*Aucune affiliation avec ByPiscine ou Arroka.*
