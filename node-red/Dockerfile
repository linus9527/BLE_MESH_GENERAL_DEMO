FROM nodered/node-red:5.0.4

WORKDIR /data
COPY package.json /data/package.json
RUN npm install --no-update-notifier --no-fund --omit=dev

COPY settings.js /data/settings.js
COPY flows.json /data/flows.json
COPY lib /data/lib
COPY scripts /data/scripts

WORKDIR /data
CMD ["node", "/data/scripts/start.js"]
