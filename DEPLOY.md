# Permanent public link (deploy once, works on every phone)
The app is container-ready (Dockerfile). Render example (needs a GitHub account):
1. Push this folder to a GitHub repository.
2. render.com -> New -> Web Service -> pick the repo -> Runtime: Docker.
3. Add a Disk: mount path /data (needed so database + certificates survive restarts; Render charges for disks).
4. Environment variables: ADMIN_EMAIL, ADMIN_PASSWORD (strong!), COLLEGE_NAME,
   SMTP_URL (e.g. smtps://smtp.gmail.com:465), SMTP_USER, SMTP_PASS (Gmail App Password), SMTP_FROM.
5. Deploy. Your link is https://<name>.onrender.com (HTTPS, so phone cameras work).
Without SMTP_* the certificate is still generated and downloadable; email status shows "test_mode".
UTC_OFFSET_MIN (default 330 = India) controls what "event date" means.

## Upgrading an existing deployment
No new environment variables. On start the server upgrades an existing database automatically (new columns/tables are added,
existing organizers are kept and marked as approved). Keep the same /data disk. After deploying, log in as admin and open
Admin Dashboard > Certificate Authority once, otherwise certificates are not issued.
