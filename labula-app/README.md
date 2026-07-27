# LABULA App

A simple React + Vite dashboard for the LABULA smart gas detection system.

## Local development

- Install dependencies: `npm install`
- Start dev server: `npm run dev`

Open the printed URL in your phone's browser on the same network.

## Deployment

This project is deployed on Vercel and auto-deploys on push to the main branch.

**Live URL:**

https://labula-system.vercel.app/

### Deploy to Vercel

1. Sign up at [vercel.com](https://vercel.com) with your GitHub account
2. Click "Add New Project" and import this repository
3. Vercel auto-detects Vite settings
4. Click "Deploy"

Your app is now live at a Vercel URL and auto-updates on every push to `master`/`main`.

## Firebase configuration

Copy `.env.example` to `.env.local` if you want to override the Firebase settings locally.

The app reads from:

- `VITE_FIREBASE_HOST`
- `VITE_FIREBASE_TOKEN`
- `VITE_FIREBASE_BASE_PATH`

## Notes

- The dashboard polls the database every 2 seconds.
- Manual override writes to `/labula_secret_2024/LABULA/control/manualOverride`.
