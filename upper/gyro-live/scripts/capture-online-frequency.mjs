import puppeteer from 'puppeteer-core';
import {existsSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
const chrome=['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true});
try {
  const page=await browser.newPage();
  await page.setViewport({width:1440,height:1100});
  await page.goto('https://gyro.233688.xyz/?v=20261001f',{waitUntil:'networkidle0'});
  const clip=await page.evaluate(()=>{
    const data=document.querySelector('.settings .grp:last-child').getBoundingClientRect();
    const can=document.querySelector('.panel.can').getBoundingClientRect();
    const column=document.querySelector('.col-dev').getBoundingClientRect();
    return {x:Math.floor(column.x),y:Math.floor(data.y),width:Math.ceil(column.width),height:Math.ceil(can.bottom-data.y)};
  });
  await page.screenshot({clip,path:join(dirname(fileURLToPath(import.meta.url)),'../../../artifacts/web-host/frequency-settings-20261001f-online.png')});
  console.log('Saved online output-frequency and CAN settings screenshot; no serial session opened');
} finally {await browser.close();}
