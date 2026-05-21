# Contribuir

Gracias por querer ayudar.

## Como

1. Abre un **issue** describiendo el bug, mejora o feature.
2. Haz fork y crea una rama: `git checkout -b feature/mi-cambio`.
3. Sigue el estilo del repo:
   - Python: PEP 8, type hints donde aporte.
   - Arduino: sin warnings al compilar; comenta los pines/funciones nuevas.
   - JS/HTML/CSS: 2 espacios, sin frameworks pesados.
4. Pon comentarios solo cuando expliquen el **por que**, no el **que**.
5. Asegurate de que el firmware **compila** y de que el servidor **arranca**
   antes de abrir el PR.
6. Abre el **Pull Request** apuntando a `main`.

## Buenas practicas

- Cambios pequenos y enfocados.
- Mensajes de commit en imperativo (`Add ...`, `Fix ...`, `Refactor ...`).
- No subas `.env`, `secrets.h` ni credenciales.
- No incluyas binarios compilados.

## Reportar un bug

Incluye:

- Pasos para reproducir.
- Logs (Serial Monitor del ESP32 + consola del servidor).
- Version de placa ESP32, librerias Arduino y Python.
